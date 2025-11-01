import subprocess
import threading
import time
import os
import re


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


class BiscaEngine:
    """
    Processo do motor C++ (bisca4*.exe) em modo "engine".
    Guarda histórico de estados parseados de 'show'.
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
    ):
        self.exe_path = os.path.abspath(exe_path)
        self.nnue_path = os.path.abspath(nnue_path) if nnue_path else ""
        self.depth = depth
        self.iterations = iterations
        self.cpuct = cpuct
        self.engine_type = engine_type
        self.perfect_info = perfect_info

        self.proc = None
        self.stdout_lock = threading.Lock()
        self.stdout_buffer = []

        self.history = []
        self.history_index = -1

    # ---------- processo I/O ----------

    def start(self):
        """Lança o engine subprocess e começa thread de leitura."""
        info_arg = "perfect" if self.perfect_info else "partial"

        if self.engine_type == "mcts":
            args = [
                self.exe_path,
                "--mode", "engine",
                "--iterations", str(self.iterations),
                "--cpuct", str(self.cpuct),
                "--info", info_arg,
            ]
            if self.nnue_path:
                args += ["--nnue", self.nnue_path]
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

        # arranca um jogo imediatamente
        self.new_game()

    def stop(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.send_cmd("quit")
                time.sleep(0.1)
                self.proc.kill()
            except Exception:
                pass
        self.proc = None

    def _reader_thread(self):
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









    def send_cmd(self, cmd):
        if self.proc is None or self.proc.poll() is not None:
            return
        self.proc.stdin.write(cmd + "\n")
        self.proc.stdin.flush()
        time.sleep(0.05)

    # ---------- high level ----------

    def new_game(self):
        self.send_cmd("newgame")
        self._drain_output()  # "Novo jogo iniciado." etc
        st = self.show_and_record(reset_history=True)
        return st

    def show_raw(self):
        self.send_cmd("show")
        out = self._read_until(lambda text: text.strip().endswith("---------------------------------"), timeout=5.0)
        return out

    def bestmove(self):
        self.send_cmd("bestmove")
        out = self._read_until(lambda text: "bestmove" in text, timeout=max(5.0, self.iterations * 0.01))
        m = re.search(r"bestmove index=(\d+)", out)
        if m:
            return int(m.group(1)), out
        return None, out

    def play_card(self, idx):
        self.send_cmd(f"play {idx}")
        play_out = self._read_until(lambda text: "Jogada" in text, timeout=5.0)
        state = self.show_and_record(reset_history=False)
        return play_out, state

    def show_and_record(self, reset_history=False):
        raw = self.show_raw()
        state = self.parse_show_state(raw)
        if reset_history:
            self.history = [state]
            self.history_index = 0
        else:
            self.history.append(state)
            self.history_index = len(self.history) - 1
        return state

    def rewind(self):
        if self.history_index > 0:
            self.history_index -= 1
        return self.history[self.history_index]

    def forward(self):
        if self.history_index < len(self.history) - 1:
            self.history_index += 1
        return self.history[self.history_index]

    def get_current_state(self):
        if 0 <= self.history_index < len(self.history):
            return self.history[self.history_index]
        return None

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
