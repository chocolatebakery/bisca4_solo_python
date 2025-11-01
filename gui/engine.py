import ctypes
import os
import platform
import re
import subprocess
import threading
import time

from typing import Optional, Tuple


def rank_to_code(rank_text):
    # "10", "A", "Q", "K", "J", "6", etc.
    if rank_text == "10":
        return "T"
    return rank_text.upper()


def suit_to_code(suit_text):
    # motor usa português: Espadas, Copas, Ouros, Paus
    s = suit_text.lower().strip()
    if s.startswith("esp"):  # Espadas
        return "S"
    if s.startswith("cop"):  # Copas
        return "H"
    if s.startswith("our"):  # Ouros
        return "D"
    if s.startswith("pau"):  # Paus
        return "C"
    return "S"


def parse_card_text(card_text):
    # "10 de Ouros"
    # "A de Espadas"
    # "6 de Espadas (naipe Espadas)" -> vamos cortar o "(naipe ...)"
    parts = card_text.split(" de ")
    if len(parts) >= 2:
        rank = parts[0].strip()
        suit = parts[1].strip()
    else:
        rank = card_text.strip()
        suit = ""

    code = rank_to_code(rank) + suit_to_code(suit)
    return {
        "rank": rank,
        "suit": suit,
        "code": code,
    }


BISCA_ENGINE_ALPHABETA = 0
BISCA_ENGINE_MCTS = 1


def _default_native_library(base_dir: str) -> str:
    system = platform.system().lower()
    if system.startswith("win"):
        return os.path.join(base_dir, "bisca4_android.dll")
    if system == "darwin":
        return os.path.join(base_dir, "libbisca4_android.dylib")
    return os.path.join(base_dir, "libbisca4_android.so")


class EngineBackend:
    def start(self) -> None:
        raise NotImplementedError

    def stop(self) -> None:
        raise NotImplementedError

    def new_game_message(self) -> str:
        raise NotImplementedError

    def show(self) -> str:
        raise NotImplementedError

    def bestmove(self) -> Tuple[Optional[int], str]:
        raise NotImplementedError

    def play(self, idx: int) -> str:
        raise NotImplementedError

    def get_status(self) -> str:
        return ""


class SubprocessBackend(EngineBackend):
    def __init__(self, exe_path, nnue_path, depth, iterations, cpuct, engine_type, perfect_info):
        self.exe_path = exe_path
        self.nnue_path = nnue_path
        self.depth = depth
        self.iterations = iterations
        self.cpuct = cpuct
        self.engine_type = engine_type
        self.perfect_info = perfect_info

        self.proc = None
        self.stdout_lock = threading.Lock()
        self.stdout_buffer = []

    def start(self) -> None:
        info_arg = "perfect" if self.perfect_info else "partial"

        if self.engine_type == "mcts":
            args = [
                self.exe_path,
                "--mode", "engine",
                "--iterations", str(self.iterations),
                "--cpuct", str(self.cpuct),
                "--info", info_arg,
            ]
        else:
            args = [
                self.exe_path,
                "--mode", "engine",
                "--depth", str(self.depth),
                "--info", info_arg,
            ]

        if self.nnue_path:
            args += ["--nnue", self.nnue_path]

        self.proc = subprocess.Popen(
            args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
            universal_newlines=True,
        )

        t = threading.Thread(target=self._reader_thread, daemon=True)
        t.start()

        time.sleep(0.2)
        self._drain_output()

    def stop(self) -> None:
        if self.proc and self.proc.poll() is None:
            try:
                self._write("quit")
                time.sleep(0.1)
                self.proc.kill()
            except Exception:
                pass
        self.proc = None

    # ---------- helpers ----------

    def _reader_thread(self):
        assert self.proc is not None
        for line in self.proc.stdout:
            with self.stdout_lock:
                self.stdout_buffer.append(line.rstrip("\n"))

    def _drain_output(self):
        with self.stdout_lock:
            out = "\n".join(self.stdout_buffer)
            self.stdout_buffer = []
        return out

    def _read_until(self, predicate, timeout=30.0, interval=0.05):
        deadline = time.time() + timeout
        parts = []
        while time.time() < deadline:
            chunk = self._drain_output()
            if chunk:
                parts.append(chunk)
                combined = "\n".join(parts)
                if predicate(combined):
                    return combined
            else:
                combined = "\n".join(parts)
                if predicate(combined):
                    return combined
            time.sleep(interval)
        return "\n".join(parts)

    def _write(self, cmd: str) -> None:
        if not self.proc or self.proc.poll() is not None:
            return
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        time.sleep(0.05)

    # ---------- public API ----------

    def new_game_message(self) -> str:
        self._write("newgame")
        return self._drain_output()

    def show(self) -> str:
        self._write("show")
        return self._read_until(lambda text: text.strip().endswith("---------------------------------"), timeout=5.0)

    def bestmove(self) -> Tuple[Optional[int], str]:
        timeout = max(5.0, self.iterations * 0.01)
        self._write("bestmove")
        out = self._read_until(lambda text: "bestmove" in text, timeout=timeout)
        m = re.search(r"bestmove index=(\d+)", out)
        return (int(m.group(1)) if m else None, out)

    def play(self, idx: int) -> str:
        self._write(f"play {idx}")
        return self._read_until(lambda text: "Jogada" in text, timeout=5.0)


class NativeBackend(EngineBackend):
    class _Config(ctypes.Structure):
        _fields_ = [
            ("type", ctypes.c_int),
            ("nnue_path", ctypes.c_char_p),
            ("depth", ctypes.c_int),
            ("iterations", ctypes.c_int),
            ("cpuct", ctypes.c_double),
            ("perfect_info", ctypes.c_int),
            ("root_mt", ctypes.c_int),
        ]

    def __init__(self, lib_path, nnue_path, depth, iterations, cpuct, engine_type, perfect_info):
        self.lib_path = lib_path
        self.nnue_path = nnue_path
        self.depth = depth
        self.iterations = iterations
        self.cpuct = cpuct
        self.engine_type = engine_type
        self.perfect_info = perfect_info

        self._lib = None
        self._handle = None
        self._status = ""

        self._nnue_path_bytes = nnue_path.encode("utf-8") if nnue_path else None
        engine_type_value = BISCA_ENGINE_ALPHABETA if engine_type == "alphabeta" else BISCA_ENGINE_MCTS
        self._config = self._Config(
            engine_type_value,
            self._nnue_path_bytes,
            depth,
            iterations,
            cpuct,
            1 if perfect_info else 0,
            0,
        )

    def start(self) -> None:
        if not os.path.exists(self.lib_path):
            raise FileNotFoundError(f"Native engine library não encontrado: {self.lib_path}")

        self._lib = ctypes.CDLL(self.lib_path)
        self._lib.bisca_engine_create.argtypes = [ctypes.POINTER(self._Config)]
        self._lib.bisca_engine_create.restype = ctypes.c_void_p
        self._lib.bisca_engine_destroy.argtypes = [ctypes.c_void_p]
        self._lib.bisca_engine_destroy.restype = None
        self._lib.bisca_engine_status.argtypes = [ctypes.c_void_p]
        self._lib.bisca_engine_status.restype = ctypes.c_char_p
        self._lib.bisca_engine_new_game.argtypes = [ctypes.c_void_p]
        self._lib.bisca_engine_new_game.restype = ctypes.c_char_p
        self._lib.bisca_engine_show.argtypes = [ctypes.c_void_p]
        self._lib.bisca_engine_show.restype = ctypes.c_char_p
        self._lib.bisca_engine_play.argtypes = [ctypes.c_void_p, ctypes.c_int]
        self._lib.bisca_engine_play.restype = ctypes.c_char_p
        self._lib.bisca_engine_bestmove.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_double)]
        self._lib.bisca_engine_bestmove.restype = ctypes.c_char_p

        handle = self._lib.bisca_engine_create(ctypes.byref(self._config))
        if not handle:
            raise RuntimeError("Falha a inicializar o engine nativo.")
        self._handle = ctypes.c_void_p(handle)

        status_ptr = self._lib.bisca_engine_status(self._handle)
        self._status = status_ptr.decode("utf-8") if status_ptr else ""

    def stop(self) -> None:
        if self._lib and self._handle:
            self._lib.bisca_engine_destroy(self._handle)
        self._handle = None
        self._lib = None

    def _ensure_handle(self):
        if not self._handle:
            raise RuntimeError("Engine nativo não inicializado.")

    def get_status(self) -> str:
        return self._status

    def new_game_message(self) -> str:
        self._ensure_handle()
        ptr = self._lib.bisca_engine_new_game(self._handle)
        return ptr.decode("utf-8") if ptr else ""

    def show(self) -> str:
        self._ensure_handle()
        ptr = self._lib.bisca_engine_show(self._handle)
        return ptr.decode("utf-8") if ptr else ""

    def bestmove(self) -> Tuple[Optional[int], str]:
        self._ensure_handle()
        idx = ctypes.c_int(-1)
        val = ctypes.c_double(0.0)
        ptr = self._lib.bisca_engine_bestmove(self._handle, ctypes.byref(idx), ctypes.byref(val))
        text = ptr.decode("utf-8") if ptr else ""
        return (idx.value if ptr else None, text)

    def play(self, idx: int) -> str:
        self._ensure_handle()
        ptr = self._lib.bisca_engine_play(self._handle, idx)
        return ptr.decode("utf-8") if ptr else ""


class BiscaEngine:
    """
    Interface de alto nível para os motores C++.
    Consegue trabalhar via subprocesso (executável) ou via biblioteca nativa.
    """

    def __init__(
        self,
        exe_path,
        nnue_path=None,
        depth=3,
        iterations=2000,
        cpuct=1.41421356,
        engine_type="alphabeta",
        perfect_info=False,
        backend=None,
        native_lib=None,
    ):
        self.exe_path = os.path.abspath(exe_path)
        self.base_dir = os.path.abspath(os.path.dirname(self.exe_path))
        self.nnue_path = os.path.abspath(nnue_path) if nnue_path else ""
        self.depth = depth
        self.iterations = iterations
        self.cpuct = cpuct
        self.engine_type = engine_type
        self.perfect_info = perfect_info

        selected_backend = backend or ("native" if os.environ.get("BISCA_ENGINE_NATIVE") else "subprocess")
        selected_backend = selected_backend.lower()

        if selected_backend == "native":
            lib_path = native_lib or _default_native_library(self.base_dir)
            self.backend = NativeBackend(
                lib_path,
                self.nnue_path,
                depth,
                iterations,
                cpuct,
                engine_type,
                perfect_info,
            )
            self.backend_name = "native"
        else:
            self.backend = SubprocessBackend(
                self.exe_path,
                self.nnue_path,
                depth,
                iterations,
                cpuct,
                engine_type,
                perfect_info,
            )
            self.backend_name = "subprocess"

        self.status_message = ""
        self.history = []
        self.history_index = -1

    # ---------- ciclo de vida ----------

    def start(self):
        self.backend.start()
        self.status_message = self.backend.get_status() or ""
        self.backend.new_game_message()
        return self.show_and_record(reset_history=True)

    def stop(self):
        self.backend.stop()

    # ---------- comandos ----------

    def new_game(self):
        self.backend.new_game_message()
        return self.show_and_record(reset_history=True)

    def show_raw(self):
        return self.backend.show()

    def bestmove(self):
        idx, out = self.backend.bestmove()
        if idx is None:
            m = re.search(r"bestmove index=(\d+)", out)
            if m:
                idx = int(m.group(1))
        return idx, out

    def play_card(self, idx):
        play_out = self.backend.play(idx)
        state = self.show_and_record(reset_history=False)
        return play_out, state

    # ---------- parsing do 'show' ----------

    def parse_show_state(self, text):
        current_player = None
        score0 = 0
        score1 = 0
        finished = False
        trump_card_obj = None
        deck_count = None
        trump_given = False

        p0_hand = []
        p1_hand = []
        trick = []

        mode = 0  # 1=P0 hand, 2=P1 hand, 3=trick

        for rawline in text.splitlines():
            line = rawline.strip()

            if line.lower().startswith("jogo terminado"):
                if "SIM" in line.upper():
                    finished = True
                continue

            if line.startswith("Trunfo:"):
                trump_part = line[len("Trunfo:"):].strip()
                paren_pos = trump_part.find(" (")
                if paren_pos != -1:
                    trump_part = trump_part[:paren_pos].strip()
                info = parse_card_text(trump_part)
                trump_card_obj = {
                    "text": trump_part,
                    "code": info["code"],
                }
                continue

            if line.startswith("Deck restante:"):
                m = re.search(r"Deck restante:\s+(\d+)", line)
                if m:
                    try:
                        deck_count = int(m.group(1))
                    except Exception:
                        deck_count = None
                continue

            if line.startswith("TrunfoDado:"):
                parts = line.split(":")
                if len(parts) >= 2:
                    try:
                        trump_given = int(parts[1].strip()) != 0
                    except Exception:
                        trump_given = False
                continue

            if line.startswith("CurrentPlayer:"):
                parts = line.split(":")
                if len(parts) >= 2:
                    try:
                        current_player = int(parts[1].strip())
                    except Exception:
                        pass
                continue

            if line.startswith("Pontuacao:"):
                m0 = re.search(r"P0=(\d+)", line)
                if m0:
                    score0 = int(m0.group(1))
                m1 = re.search(r"P1=(\d+)", line)
                if m1:
                    score1 = int(m1.group(1))
                continue

            low = line.lower()
            if low.startswith("mao p0"):
                mode = 1
                continue
            if low.startswith("mao p1"):
                mode = 2
                continue
            if low.startswith("trick atual"):
                mode = 3
                continue
            if line.startswith("---------------------------------"):
                mode = 0
                continue

            # cartas tipo:
            # [0] 10 de Ouros
            # [1] A de Espadas
            # (0) Q de Copas
            if line.startswith("[") or line.startswith("("):
                parts = line.split(" ", 1)
                if len(parts) < 2:
                    continue
                raw_idx = parts[0].strip("[]()")
                card_txt = parts[1].strip()
                try:
                    idx_num = int(raw_idx)
                except Exception:
                    idx_num = 0

                ci = parse_card_text(card_txt)
                ci["index"] = idx_num

                if mode == 1:
                    p0_hand.append(ci)
                elif mode == 2:
                    p1_hand.append(ci)
                elif mode == 3:
                    trick.append(ci)
                continue

        return {
            "p0_hand": p0_hand,
            "p1_hand": p1_hand,
            "trick": trick,

            "trump": trump_card_obj,
            "deck_count": deck_count,
            "trump_given": trump_given,

            "score0": score0,
            "score1": score1,

            "current_player": current_player,
            "finished": finished,

            "raw": text,
        }
