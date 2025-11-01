import tkinter as tk
from tkinter import messagebox
from PIL import Image, ImageTk
import os
from engine import BiscaEngine
from collections import OrderedDict

# ---------- Aparência / Layout Constantes ----------

CARD_W = 100
CARD_H = 150

TABLE_BG = "#063b06"          # feltro
PANEL_BG = "#1f1f1f"          # painel scoreboard topo
PANEL_OUTLINE = "#bfbfbf"
TEXT_MAIN = "#ffffff"
TEXT_SUB = "#cccccc"
TEXT_WARN = "#ffcc00"

HUMAN_COLOR = "#79ff79"
AI_COLOR = "#ff6f6f"
LAST_HILITE_BG = "#b34700"
LAST_HILITE_FG = "#ffffff"

# coordenadas principais
OFFSET_X = 140
P0_X = 200    # mão jogador
P0_Y = 520
P1_X = 200    # mão IA
P1_Y = 120
TRICK_X = 300
TRICK_Y = 300
TRUMP_X = 40
TRUMP_Y = 300

# scoreboard painel à esquerda topo
SCOREBOARD_X = 80
SCOREBOARD_Y = 20
SCOREBLOCK_W = 600
SCOREBLOCK_H = 90

# histórico painel à direita topo
ROUND_HISTORY_X = 700
ROUND_HISTORY_Y = 20
ROUND_HISTORY_W = 520
ROUND_HISTORY_H = 90

# painéis de capturas à direita
CAP_AI_X1 = 920
CAP_AI_Y1 = 130
CAP_AI_X2 = 1220
CAP_AI_Y2 = 330

CAP_P0_X1 = 920
CAP_P0_Y1 = 360
CAP_P0_X2 = 1220
CAP_P0_Y2 = 560

PARTIDAS_TARGET = 4  # apenas decorativo

# escala mini para cartas nas capturas
CAP_SCALE = 0.4
CAP_FAN_OFFSET = 26  # offset horizontal entre cartas nas capturas (px)


class BiscaGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("Bisca dos 4")

        # estado engine / jogo
        self.engine = None
        self.current_state = None
        self.prev_state = None

        # scoreboard eterno
        self.match_partidas_p0 = 0
        self.match_partidas_p1 = 0
        self.round_history = []  # [{"p0_gain":x,"p1_gain":y}, ...]

        # capturas de vaza
        self.captured_p0 = []
        self.captured_p1 = []

        # qual jogador deve começar a próxima mão:
        # 0 = humano, 1 = IA. Default humano.
        self.next_start_player = 0

        # última jogada anunciada ("IA jogou AS", etc)
        self.last_play_description = ""

        # estado de animação de recolha (fallback)
        self._collecting = False
        self._collect_started_ms = 0

        # assets de clique / animação
        self.images_cache = {}
        self.click_zones = []
        self.last_click_anim_info = None
        self.last_ai_anim_info = None

        self.base_dir = os.path.abspath(os.path.dirname(__file__))

        # perfis de dificuldade (cada um mapeia para o motor pretendido)
        self.difficulty_profiles = OrderedDict([
            ("Fácil",   {"engine": "alphabeta", "exe": "bisca4.exe",       "depth": 4,    "nnue": "nnue_ab.bin"}),
            ("Médio",   {"engine": "mcts",      "exe": "bisca4_mcts.exe", "iterations": 2200, "cpuct": 1.35, "nnue": "nnue_mid.bin"}),
            ("Difícil", {"engine": "mcts",      "exe": "bisca4_mcts.exe", "iterations": 4200, "cpuct": 1.40, "nnue": "nnue_hard.bin"}),
        ])

        initial_diff = "Médio"
        self.difficulty = tk.StringVar(value=initial_diff)

        # runtime config
        initial_profile = self.difficulty_profiles[initial_diff]
        initial_profile = self.difficulty_profiles[initial_diff]
        exe_name = initial_profile.get("exe", "bisca4.exe")
        self.current_engine_exe = os.path.join(self.base_dir, exe_name)
        self.engine_type = initial_profile.get("engine", "alphabeta")
        self.depth = initial_profile.get("depth", 4)
        self.iterations = initial_profile.get("iterations", 2000)
        self.cpuct = initial_profile.get("cpuct", 1.41421356)
        nnue_name = initial_profile.get("nnue", "")
        self.nnue_path = os.path.join(self.base_dir, nnue_name) if nnue_name else ""
        self.auto_play_p1 = True    # IA joga automática
        self.ai_delay_ms = 1600

        # UI setup
        self.canvas = tk.Canvas(
            root,
            width=1280,
            height=800,
            bg=TABLE_BG,
            highlightthickness=0
        )
        self.canvas.grid(row=0, column=0, columnspan=5, sticky="nsew")

        self.diff_menu = tk.OptionMenu(
            root,
            self.difficulty,
            *self.difficulty_profiles.keys(),
            command=self.on_change_difficulty
        )
        self.diff_menu.grid(row=1, column=0, sticky="ew")

        self.btn_new_hand = tk.Button(root, text="Nova Mão", command=self.on_new_hand_button)
        self.btn_new_hand.grid(row=1, column=1, sticky="ew")

        self.btn_reset_match = tk.Button(root, text="Reset Histórico", command=self.on_reset_match)
        self.btn_reset_match.grid(row=1, column=2, sticky="ew")

        self.btn_best = tk.Button(root, text="AI sugere", command=self.on_ai_bestmove)
        self.btn_best.grid(row=1, column=3, sticky="ew")

        self.btn_ai_play = tk.Button(root, text="AI joga agora", command=self.on_ai_play_button)
        self.btn_ai_play.grid(row=1, column=4, sticky="ew")

        self.btn_rewind = tk.Button(root, text="<<", command=self.on_rewind)
        self.btn_rewind.grid(row=2, column=0, sticky="ew")
        self.btn_forward = tk.Button(root, text=">>", command=self.on_forward)
        self.btn_forward.grid(row=2, column=1, sticky="ew")

        self.log_box = tk.Text(
            root,
            height=8,
            width=60,
            bg="#000000",
            fg="#aaffaa",
            insertbackground="white"
        )
        self.log_box.grid(row=2, column=2, columnspan=3, sticky="nsew")

        root.columnconfigure(4, weight=1)
        root.rowconfigure(0, weight=1)

        self.canvas.bind("<Button-1>", self.on_canvas_click)

        # arranque
        self.apply_difficulty_profile("Médio")
        self.start_engine()

    # ---------------- utils / mensagens ----------------

    def log(self, text):
        if text:
            self.log_box.insert("1.0", text.strip() + "\n")

    def popup(self, msg):
        messagebox.showinfo("Bisca dos 4", msg)

    # ---------------- imagens cartas ----------------

    def get_card_image(self, code, face_down=False, scale=1.0):
        key = f"{'blank' if face_down else code}_x{scale}"
        if key in self.images_cache:
            return self.images_cache[key]

        filename = "blank.png" if face_down else (code + ".png")
        path = os.path.join("assets", "cards", filename)
        if not os.path.exists(path):
            path = os.path.join("assets", "cards", "blank.png")

        w = int(CARD_W * scale)
        h = int(CARD_H * scale)
        img = Image.open(path).resize((w, h), Image.LANCZOS)
        photo = ImageTk.PhotoImage(img)
        self.images_cache[key] = photo
        return photo

    def draw_single_card_temp(self, code, x, y, face_down=False):
        img = self.get_card_image(code, face_down=face_down, scale=1.0)
        return self.canvas.create_image(x, y, anchor="nw", image=img)

    # animação suave (curva quadrática + easing)
    def _ease_out_cubic(self, t):
        t = max(0.0, min(1.0, t))
        return 1.0 - (1.0 - t) ** 3

    def _ease_in_out_cubic(self, t):
        t = max(0.0, min(1.0, t))
        if t < 0.5:
            return 4 * t * t * t
        f = (2 * t - 2)
        return 0.5 * f * f * f + 1

    def animate_move_curve(self, item_id, x0, y0, x1, y1, ctrl_dx=0, ctrl_dy=-40, duration_ms=450, ease='out', on_done=None):
        steps = max(1, int(duration_ms / 16))
        mx = (x0 + x1) * 0.5 + ctrl_dx
        my = (y0 + y1) * 0.5 + ctrl_dy

        def bezier(t):
            u = 1.0 - t
            bx = u*u*x0 + 2*u*t*mx + t*t*x1
            by = u*u*y0 + 2*u*t*my + t*t*y1
            return bx, by

        def step(i=0):
            if i > steps:
                if on_done:
                    on_done()
                return
            raw = i / steps
            tt = self._ease_in_out_cubic(raw) if ease == 'inout' else self._ease_out_cubic(raw)
            bx, by = bezier(tt)
            self.canvas.coords(item_id, bx, by)
            self.root.after(16, lambda: step(i+1))

        step(0)

    # ---------------- dificuldade / motor ----------------

    def apply_difficulty_profile(self, name):
        prof = self.difficulty_profiles.get(name)
        if not prof:
            return

        wanted_nnue = prof.get("nnue", self.nnue_path)
        if wanted_nnue:
            nnue_candidate = os.path.join(self.base_dir, wanted_nnue)
            if not os.path.exists(nnue_candidate):
                fallback = None
                for cand in sorted(os.listdir(self.base_dir)):
                    if cand.startswith("nnue_iter") and cand.endswith(".bin"):
                        fallback = os.path.join(self.base_dir, cand)
                if fallback:
                    nnue_candidate = fallback
                else:
                    nnue_candidate = ""
            self.nnue_path = nnue_candidate if nnue_candidate else ""
        else:
            self.nnue_path = ""

        exe_name = prof.get("exe", "bisca4.exe")
        self.current_engine_exe = os.path.join(self.base_dir, exe_name)
        self.engine_type = prof.get("engine", "alphabeta")

        if self.engine_type == "mcts":
            self.iterations = prof.get("iterations", self.iterations)
            self.cpuct = prof.get("cpuct", self.cpuct)
            self.depth = prof.get("depth", self.depth)
        else:
            self.depth = prof.get("depth", self.depth)
            # mantém últimos valores usados para eventual regresso a MCTS
            self.iterations = prof.get("iterations", self.iterations)
            self.cpuct = prof.get("cpuct", self.cpuct)

    def start_engine(self):
        if self.engine:
            self.engine.stop()

        self.engine = BiscaEngine(
            exe_path=self.current_engine_exe,
            nnue_path=self.nnue_path,
            depth=self.depth,
            iterations=self.iterations,
            cpuct=self.cpuct,
            engine_type=self.engine_type,
            perfect_info=False
        )
        self.engine.start()

        nnue_label = os.path.basename(self.nnue_path) if self.nnue_path else "none"
        if self.engine_type == "mcts":
            self.log(f"[GUI] Motor ON (MCTS) | NNUE={nnue_label} iter={self.iterations} cpuct={self.cpuct}")
        else:
            self.log(f"[GUI] Motor ON (AlphaBeta) | NNUE={nnue_label} depth={self.depth}")
        self.current_state = self.engine.get_current_state()

        # reset mesa da mão
        self.captured_p0 = []
        self.captured_p1 = []
        self.last_click_anim_info = None
        self.last_ai_anim_info = None

        # Quando arrancamos de raiz, assumimos humano começa por default.
        self.next_start_player = 0
        self.last_play_description = ""

        self.redraw(replay_mode=False)

    # ---------------- botões topo ----------------

    def on_change_difficulty(self, *_):
        # muda dificuldade e relança o motor (mantém histórico de partidas e round_history!)
        diff_name = self.difficulty.get()
        self.apply_difficulty_profile(diff_name)
        self.start_engine()

    def on_new_hand_button(self):
        # força nova mão (mantém scoreboard), respeitando next_start_player
        if not self.engine:
            return
        self.start_new_hand_same_match()

    def on_reset_match(self):
        # limpa tudo
        self.match_partidas_p0 = 0
        self.match_partidas_p1 = 0
        self.round_history = []
        self.start_engine()

    def start_new_hand_same_match(self):
        st = self.engine.new_game()
        self.prev_state = None
        self.current_state = st
        self.captured_p0 = []
        self.captured_p1 = []
        self.last_click_anim_info = None
        self.last_ai_anim_info = None
        self.last_play_description = ""

        self.redraw(replay_mode=False)

        # respeitar quem deveria começar
        self.maybe_force_starting_player()

    def maybe_force_starting_player(self):
        """
        Se a próxima mão devia ser começada pela IA (self.next_start_player == 1)
        e o current_player atual do engine é 1, deixamos a IA jogar logo.
        """
        if not self.current_state:
            return
        if self.current_state.get("finished"):
            return

        if self.next_start_player == 1 and self.current_state.get("current_player") == 1:
            # força jogada inicial da IA com pequeno delay (parece mais 'humano')
            self.root.after(self.ai_delay_ms, self.force_ai_move)

    # ---------------- outros botões ----------------

    def on_ai_bestmove(self):
        if not self.engine:
            return
        idx, raw = self.engine.bestmove()
        if idx is None:
            self.log("[AI] sem jogada válida")
        else:
            self.log(f"[AI] sugere idx {idx}")
        self.log(raw)

        self.current_state = self.engine.get_current_state()
        self.redraw(replay_mode=False)

    def on_ai_play_button(self):
        self.force_ai_move()

    def on_rewind(self):
        if not self.engine:
            return
        st = self.engine.rewind()
        self.prev_state = None
        self.current_state = st
        self.redraw(replay_mode=True)

    def on_forward(self):
        if not self.engine:
            return
        st = self.engine.forward()
        self.prev_state = None
        self.current_state = st
        self.redraw(replay_mode=True)

    # ---------------- clique do jogador ----------------

    def on_canvas_click(self, event):
        if not self.engine or not self.current_state:
            return
        if self.current_state.get("finished"):
            return

        cp = self.current_state.get("current_player")
        if cp != 0 and not self.auto_play_p0:
            return

        clicked_zone = None
        for zone in self.click_zones:
            x1, y1, x2, y2 = zone["bbox"]
            if x1 <= event.x <= x2 and y1 <= event.y <= y2:
                clicked_zone = zone
                break
        if not clicked_zone:
            return

        idx_to_play = clicked_zone["card_index"]
        code_clicked = clicked_zone["code"]
        origin_x = clicked_zone["x"]
        origin_y = clicked_zone["y"]

        self.last_click_anim_info = {
            "code": code_clicked,
            "x": origin_x,
            "y": origin_y,
            "face_down": False
        }

        self.log(f"[P0] joga idx {idx_to_play} ({code_clicked})")
        out, st_after = self.engine.play_card(idx_to_play)
        self.log(out)

        prev = self.current_state
        self.prev_state = prev
        self.current_state = st_after

        self.animate_transition(prev, st_after, player_who_played=0, callback_after=self.redraw_final)

    # ---------------- IA automática ----------------

    def maybe_schedule_ai(self):
        """
        Chamado depois de redraw() (normal).
        Se for a vez da IA e o jogo não acabou, agendamos jogada dela.
        """
        if not self.engine or not self.current_state:
            return
        if self.current_state.get("finished"):
            return

        cp = self.current_state.get("current_player")
        if cp == 1 and self.auto_play_p1:
            self.root.after(self.ai_delay_ms, self.force_ai_move)

    def force_ai_move(self):
        if not self.engine or not self.current_state:
            return
        if self.current_state.get("finished"):
            return

        st = self.current_state
        cp = st.get("current_player")
        if cp != 1 and self.auto_play_p1:
            return

        idx, raw = self.engine.bestmove()
        if idx is None:
            self.log("[AI] não há jogada válida (?)")
            self.log(raw)
            self.current_state = self.engine.get_current_state()
            self.redraw(replay_mode=False)
            return

        self.log(f"[AI] joga idx {idx}")
        self.log(raw)

        # capturar coords da carta da IA antes de mandar jogar
        ai_hand = st.get("p1_hand", [])
        self.last_ai_anim_info = None
        for i, card in enumerate(ai_hand):
            if card.get("index") == idx:
                card_code = card["code"]
                origin_x = P1_X + i * OFFSET_X
                origin_y = P1_Y
                self.last_ai_anim_info = {
                    "code": card_code,
                    "x": origin_x,
                    "y": origin_y,
                    "face_down": True
                }
                break

        out, st_after = self.engine.play_card(idx)
        self.log(out)

        prev = self.current_state
        self.prev_state = prev
        self.current_state = st_after

        self.animate_transition(prev, st_after, player_who_played=1, callback_after=self.redraw_final)

    # ---------------- animação / transição ----------------

    def animate_transition(self, prev, curr, player_who_played, callback_after=None):
        """
        Detecta:
        - Uma carta nova na trick -> anima da mão para a mesa.
          Também atualiza self.last_play_description = "Tu jogaste XX" / "IA jogou XX".
        - A trick cheia recolhida -> anima para o vencedor e guarda as cartas capturadas.
        No final, chama callback_after(), e depois faz check_end_of_hand().
        """
        if prev is None or curr is None:
            if callback_after:
                callback_after()
            return

        prev_trick = prev.get("trick", [])
        curr_trick = curr.get("trick", [])

        # Revert path: handle only simple cases first and return
        if len(curr_trick) == len(prev_trick) + 1 and curr_trick:
            code = curr_trick[-1]["code"]
            if player_who_played == 0:
                self.last_play_description = f"Tu jogaste {code}"
            else:
                self.last_play_description = f"IA jogou {code}"

            dest_x = TRICK_X + (len(curr_trick) - 1) * (CARD_W + 10)
            dest_y = TRICK_Y

            start_x = None
            start_y = None
            face_down = (player_who_played == 1)

            p0_before = set(c["code"] for c in prev.get("p0_hand", []))
            p0_after  = set(c["code"] for c in curr.get("p0_hand", []))
            p1_before = set(c["code"] for c in prev.get("p1_hand", []))
            p1_after  = set(c["code"] for c in curr.get("p1_hand", []))

            played_by_p0 = (code in p0_before and code not in p0_after)
            played_by_p1 = (code in p1_before and code not in p1_after)

            if played_by_p0 and self.last_click_anim_info and self.last_click_anim_info.get("code") == code:
                start_x = self.last_click_anim_info["x"]
                start_y = self.last_click_anim_info["y"]
                face_down = False
            elif played_by_p1 and self.last_ai_anim_info and self.last_ai_anim_info.get("code") == code:
                start_x = self.last_ai_anim_info["x"]
                start_y = self.last_ai_anim_info["y"]
                face_down = self.last_ai_anim_info["face_down"]

            if start_x is None:
                if played_by_p0:
                    start_x = P0_X + max(0, len(prev.get("p0_hand", [])) - 1) * OFFSET_X
                    start_y = P0_Y
                    face_down = False
                elif played_by_p1:
                    start_x = P1_X + max(0, len(prev.get("p1_hand", [])) - 1) * OFFSET_X
                    start_y = P1_Y
                    face_down = True
                else:
                    start_x = TRICK_X
                    start_y = TRICK_Y
                    face_down = False

            temp_id = self.draw_single_card_temp(code, start_x, start_y, face_down=face_down)
            self.animate_move_curve(temp_id, start_x, start_y, dest_x, dest_y,
                                    ctrl_dx=0, ctrl_dy=-30, duration_ms=450, ease='out',
                                    on_done=callback_after)
            return

        if len(prev_trick) == 4 and len(curr_trick) == 0:
            winner = curr.get("current_player", 0)
            dest_x = P0_X if winner == 0 else P1_X
            dest_y = P0_Y if winner == 0 else P1_Y
            base_x = TRICK_X
            base_y = TRICK_Y

            temp_ids = []
            for i, card in enumerate(prev_trick):
                code = card["code"]
                cid = self.draw_single_card_temp(code, base_x + i * (CARD_W + 10), base_y, face_down=False)
                temp_ids.append(cid)

            if winner == 0:
                self.captured_p0.extend([c["code"] for c in prev_trick])
            else:
                self.captured_p1.extend([c["code"] for c in prev_trick])

            steps = 10
            dx = (dest_x - base_x) / steps
            dy = (dest_y - base_y) / steps

            def step_anim2(i=0):
                if i >= steps:
                    if callback_after:
                        callback_after()
                    self.check_end_of_hand(curr)
                    return
                for cid in temp_ids:
                    self.canvas.move(cid, dx, dy)
                self.root.after(16, lambda: step_anim2(i+1))

            step_anim2()
            return

        # Caso especial: a 4ª carta foi jogada e o motor já fechou a vaza (3 -> 0)
        # Animamos primeiro a 4ª carta da mão para a mesa, esperamos 3s e só depois
        # animamos a recolha das 4 cartas para o vencedor.
        if len(prev_trick) == 3 and len(curr_trick) == 0:
            # descobrir a 4ª carta pelo delta das mãos
            p0_before = set(c["code"] for c in prev.get("p0_hand", []))
            p0_after  = set(c["code"] for c in curr.get("p0_hand", []))
            p1_before = set(c["code"] for c in prev.get("p1_hand", []))
            p1_after  = set(c["code"] for c in curr.get("p1_hand", []))

            last_code = None
            if player_who_played == 0:
                diff = list(p0_before - p0_after)
                if diff:
                    last_code = diff[0]
            else:
                diff = list(p1_before - p1_after)
                if diff:
                    last_code = diff[0]

            prev_trick_used = list(prev_trick)
            if last_code:
                prev_trick_used.append({"code": last_code})

            # vencedor (current_player do estado atual)
            winner = curr.get("current_player", 0)
            dest_x = P0_X if winner == 0 else P1_X
            dest_y = P0_Y if winner == 0 else P1_Y

            # anima 4ª carta para a mesa (posição 3)
            def do_collect():
                # atualizar capturas apenas no momento da recolha
                if winner == 0:
                    self.captured_p0.extend([c["code"] for c in prev_trick_used])
                else:
                    self.captured_p1.extend([c["code"] for c in prev_trick_used])

                base_x = TRICK_X
                base_y = TRICK_Y
                temp_ids = []
                for i, card in enumerate(prev_trick_used):
                    code = card["code"]
                    cid = self.draw_single_card_temp(code,
                        base_x + i * (CARD_W + 10), base_y, face_down=False)
                    temp_ids.append(cid)

                if not temp_ids:
                    if callback_after:
                        callback_after()
                    return

                remaining = [len(temp_ids)]

                def one_done():
                    remaining[0] -= 1
                    if remaining[0] <= 0:
                        if callback_after:
                            callback_after()

                for i, cid in enumerate(temp_ids):
                    sx = base_x + i * (CARD_W + 10)
                    sy = base_y
                    # pequeno atraso entre cartas para efeito cascata
                    self.root.after(i * 120, lambda sx_=sx, sy_=sy, cid_=cid: self.animate_move_curve(
                        cid_, sx_, sy_, dest_x, dest_y, ctrl_dx=0, ctrl_dy=-40,
                        duration_ms=500, ease='inout',
                        on_done=lambda cid_=cid_: (self.canvas.delete(cid_), one_done())[1]
                    ))

            if last_code:
                # origem para a animação da 4ª carta
                start_x = None
                start_y = None
                face_down = (player_who_played == 1)
                if player_who_played == 0 and self.last_click_anim_info and self.last_click_anim_info.get("code") == last_code:
                    start_x = self.last_click_anim_info["x"]
                    start_y = self.last_click_anim_info["y"]
                    face_down = False
                elif player_who_played == 1 and self.last_ai_anim_info and self.last_ai_anim_info.get("code") == last_code:
                    start_x = self.last_ai_anim_info["x"]
                    start_y = self.last_ai_anim_info["y"]
                    face_down = self.last_ai_anim_info["face_down"]
                if start_x is None:
                    if player_who_played == 0:
                        start_x = P0_X + max(0, len(prev.get("p0_hand", [])) - 1) * OFFSET_X
                        start_y = P0_Y
                        face_down = False
                    else:
                        start_x = P1_X + max(0, len(prev.get("p1_hand", [])) - 1) * OFFSET_X
                        start_y = P1_Y
                        face_down = True

                dest4_x = TRICK_X + 3 * (CARD_W + 10)
                dest4_y = TRICK_Y
                temp_id = self.draw_single_card_temp(last_code, start_x, start_y, face_down=face_down)
                def after_card_on_table():
                    self.root.after(300, do_collect)
                self.animate_move_curve(
                    temp_id, start_x, start_y, dest4_x, dest4_y,
                    ctrl_dx=0, ctrl_dy=-35, duration_ms=500, ease='out',
                    on_done=lambda: (self.canvas.delete(temp_id), after_card_on_table())[1]
                )
            else:
                # sem 4ª carta deduzida: aguardar 3s e recolher
                self.root.after(1, do_collect)
            return

        # 1) Trick cresceu 1 carta
        if len(curr_trick) == len(prev_trick) + 1:
            new_card = curr_trick[-1]
            code = new_card["code"]

            # mensagem da última jogada
            if player_who_played == 0:
                self.last_play_description = f"Tu jogaste {code}"
            else:
                self.last_play_description = f"IA jogou {code}"

            dest_x = TRICK_X + (len(curr_trick) - 1) * (CARD_W + 10)
            dest_y = TRICK_Y

            start_x = None
            start_y = None
            face_down = False

            p0_before = set(c["code"] for c in prev.get("p0_hand", []))
            p0_after  = set(c["code"] for c in curr.get("p0_hand", []))
            p1_before = set(c["code"] for c in prev.get("p1_hand", []))
            p1_after  = set(c["code"] for c in curr.get("p1_hand", []))

            played_by_p0 = (code in p0_before and code not in p0_after)
            played_by_p1 = (code in p1_before and code not in p1_after)

            # usar coords guardadas se temos
            if played_by_p0 and self.last_click_anim_info and self.last_click_anim_info["code"] == code:
                start_x = self.last_click_anim_info["x"]
                start_y = self.last_click_anim_info["y"]
                face_down = False
            elif played_by_p1 and self.last_ai_anim_info and self.last_ai_anim_info["code"] == code:
                start_x = self.last_ai_anim_info["x"]
                start_y = self.last_ai_anim_info["y"]
                face_down = self.last_ai_anim_info["face_down"]

            # fallback
            if start_x is None:
                if played_by_p0:
                    start_x = P0_X + (len(prev.get("p0_hand", [])) - 1) * OFFSET_X
                    start_y = P0_Y
                    face_down = False
                elif played_by_p1:
                    start_x = P1_X + (len(prev.get("p1_hand", [])) - 1) * OFFSET_X
                    start_y = P1_Y
                    face_down = True
                else:
                    start_x = TRICK_X
                    start_y = TRICK_Y
                    face_down = False

            temp_id = self.draw_single_card_temp(code, start_x, start_y, face_down=face_down)
            self.animate_move_curve(
                temp_id,
                start_x, start_y,
                dest_x, dest_y,
                ctrl_dx=0, ctrl_dy=-30,
                duration_ms=450,
                ease='out',
                on_done=callback_after
            )
            return

        # 2) Trick recolhida / casos 3->0 ou 3->1 (motor já fechou e iniciou nova vaza)
        if (len(prev_trick) >= 3 and len(curr_trick) <= 1) and not getattr(self, "_collecting", False):
            self._collecting = True
            prev_trick_used = list(prev_trick)
            last_code = None
            # se o estado atual fornecer a última vaza completa, usar diretamente
            lt = curr.get("last_trick", [])
            if lt and len(lt) >= 4:
                prev_trick_used = lt
                last_code = lt[-1]["code"] if isinstance(lt[-1], dict) and "code" in lt[-1] else None
            if len(prev_trick) == 3:
                p0_before = set(c["code"] for c in prev.get("p0_hand", []))
                p0_after  = set(c["code"] for c in curr.get("p0_hand", []))
                p1_before = set(c["code"] for c in prev.get("p1_hand", []))
                p1_after  = set(c["code"] for c in curr.get("p1_hand", []))
                if not lt:
                    if player_who_played == 0:
                        diff = list(p0_before - p0_after)
                        if diff:
                            last_code = diff[0]
                    else:
                        diff = list(p1_before - p1_after)
                        if diff:
                            last_code = diff[0]
                    if last_code and len(prev_trick_used) == 3:
                        prev_trick_used.append({"code": last_code})

            winner = curr.get("current_player", 0)
            dest_x = P0_X if winner == 0 else P1_X
            dest_y = P0_Y if winner == 0 else P1_Y

            base_x = TRICK_X
            base_y = TRICK_Y - (8 if len(curr_trick) == 1 else 0)

            collected = [False]
            def do_collect():
                if collected[0]:
                    return
                collected[0] = True
                # desenhar 4 cartas no centro e animar em cascata para o vencedor
                temp_ids = []
                for i, card in enumerate(prev_trick_used):
                    code = card["code"]
                    cid = self.draw_single_card_temp(code, base_x + i * (CARD_W + 10), base_y, face_down=False)
                    temp_ids.append(cid)
                # atualizar capturas só aqui
                if winner == 0:
                    self.captured_p0.extend([c["code"] for c in prev_trick_used])
                else:
                    self.captured_p1.extend([c["code"] for c in prev_trick_used])

                remaining = [len(temp_ids)]
                def one_done():
                    remaining[0] -= 1
                    if remaining[0] <= 0:
                        if callback_after:
                            callback_after()
                        self.check_end_of_hand(curr)
                        self._collecting = False
                for i, cid in enumerate(temp_ids):
                    sx = base_x + i * (CARD_W + 10)
                    sy = base_y
                    self.root.after(i * 120, lambda cid_=cid, sx_=sx, sy_=sy: self.animate_move_curve(
                        cid_, sx_, sy_, dest_x, dest_y, ctrl_dx=0, ctrl_dy=-40, duration_ms=500, ease='inout', on_done=one_done
                    ))

            if last_code:
                # Anima a 4ª carta da mão para a mesa rapidamente
                start_x = None
                start_y = None
                face_down = (player_who_played == 1)
                if player_who_played == 0 and self.last_click_anim_info and self.last_click_anim_info.get("code") == last_code:
                    start_x = self.last_click_anim_info["x"]
                    start_y = self.last_click_anim_info["y"]
                    face_down = False
                elif player_who_played == 1 and self.last_ai_anim_info and self.last_ai_anim_info.get("code") == last_code:
                    start_x = self.last_ai_anim_info["x"]
                    start_y = self.last_ai_anim_info["y"]
                    face_down = self.last_ai_anim_info["face_down"]
                if start_x is None:
                    if player_who_played == 0:
                        start_x = P0_X + max(0, len(prev.get("p0_hand", [])) - 1) * OFFSET_X
                        start_y = P0_Y
                        face_down = False
                    else:
                        start_x = P1_X + max(0, len(prev.get("p1_hand", [])) - 1) * OFFSET_X
                        start_y = P1_Y
                        face_down = True
                dest4_x = base_x + 3 * (CARD_W + 10)
                dest4_y = base_y
                temp_id = self.draw_single_card_temp(last_code, start_x, start_y, face_down=face_down)
                def after_card_on_table():
                    self.root.after(1500, do_collect)
                self.animate_move_curve(temp_id, start_x, start_y, dest4_x, dest4_y,
                                         ctrl_dx=0, ctrl_dy=-35, duration_ms=400, ease='out',
                                         on_done=lambda: (self.canvas.delete(temp_id), after_card_on_table())[1])
            else:
                self.root.after(1500, do_collect)
            return

        # fallback
        if callback_after:
            callback_after()
        self.check_end_of_hand(curr)

    def redraw_final(self):
        self.redraw(replay_mode=False)
        self.check_end_of_hand(self.current_state)

    # ---------------- pontuação / fim de mão ----------------

    def calc_partidas_gain(self, p0_points, p1_points):
        if p0_points > p1_points:
            loser_pts = p1_points
            if loser_pts == 0:   val = 4
            elif loser_pts <= 25: val = 2
            else:                val = 1
            return val, 0
        elif p1_points > p0_points:
            loser_pts = p0_points
            if loser_pts == 0:   val = 4
            elif loser_pts <= 25: val = 2
            else:                val = 1
            return 0, val
        else:
            return 0, 0

    def check_end_of_hand(self, st):
        """
        Se a mão terminou:
          - calcular ganhos de partidas
          - atualizar scoreboard global e histórico
          - guardar quem começa a próxima mão (current_player final)
          - popup resumo
          - arrancar nova mão e, se IA deve começar, deixá-la jogar logo
        """
        if not st:
            return
        if not st.get("finished"):
            return

        p0_pts = st.get("score0", 0)
        p1_pts = st.get("score1", 0)

        gain_p0, gain_p1 = self.calc_partidas_gain(p0_pts, p1_pts)
        self.match_partidas_p0 += gain_p0
        self.match_partidas_p1 += gain_p1

        # histórico tipo lichess
        self.round_history.append({
            "p0_gain": gain_p0,
            "p1_gain": gain_p1,
        })

        # quem ganhou a última vaza é quem está em current_player
        # este jogador vai começar a próxima mão
        self.next_start_player = st.get("current_player", 0)

        msg = (
            f"Mão terminada.\n"
            f"Tu: {p0_pts} pontos\n"
            f"IA: {p1_pts} pontos\n\n"
            f"Partidas nesta mão: Tu +{gain_p0}, IA +{gain_p1}\n"
            f"Total partidas: Tu {self.match_partidas_p0} - IA {self.match_partidas_p1}\n"
            f"Próxima mão começa: {'Tu' if self.next_start_player==0 else 'IA'}"
        )
        self.popup(msg)

        # nova mão imediatamente
        self.start_new_hand_same_match()

    # ---------------- desenho das coisas bonitas ----------------

    def draw_rounded_rect(self, x1, y1, x2, y2, r, fill, outline, width=2):
        # desenha retângulo arredondado usando vários create_* (tk não tem nativo)
        self.canvas.create_arc(x1, y1, x1+2*r, y1+2*r,
                               start=90, extent=90,
                               style="pieslice",
                               fill=fill, outline=outline, width=width)
        self.canvas.create_arc(x2-2*r, y1, x2, y1+2*r,
                               start=0, extent=90,
                               style="pieslice",
                               fill=fill, outline=outline, width=width)
        self.canvas.create_arc(x1, y2-2*r, x1+2*r, y2,
                               start=180, extent=90,
                               style="pieslice",
                               fill=fill, outline=outline, width=width)
        self.canvas.create_arc(x2-2*r, y2-2*r, x2, y2,
                               start=270, extent=90,
                               style="pieslice",
                               fill=fill, outline=outline, width=width)

        self.canvas.create_rectangle(x1+r, y1, x2-r, y2,
                                     fill=fill, outline=outline, width=width)
        self.canvas.create_rectangle(x1, y1+r, x1+r, y2-r,
                                     fill=fill, outline=outline, width=width)
        self.canvas.create_rectangle(x2-r, y1+r, x2, y2-r,
                                     fill=fill, outline=outline, width=width)

    def draw_text_shadow(self, x, y, text, color_fg, font, anchor="nw"):
        # sombra preta 1px e depois texto
        self.canvas.create_text(
            x+1, y+1,
            text=text,
            fill="#000000",
            font=font,
            anchor=anchor
        )
        self.canvas.create_text(
            x, y,
            text=text,
            fill=color_fg,
            font=font,
            anchor=anchor
        )

    def draw_scoreboard_panel(self, st):
        """
        Painel do lado esquerdo topo:
        - Pontos mão
        - Partidas acumuladas
        - Vez / TERMINADO
        - Dica partidas alvo
        """
        p0_pts = st.get("score0", 0)
        p1_pts = st.get("score1", 0)
        cp = st.get("current_player")
        finished = st.get("finished")
        turn_txt = "TERMINADO" if finished else f"Vez: P{cp}"

        x1 = SCOREBOARD_X
        y1 = SCOREBOARD_Y
        x2 = x1 + SCOREBLOCK_W
        y2 = y1 + SCOREBLOCK_H

        self.draw_rounded_rect(
            x1, y1, x2, y2,
            r=12,
            fill=PANEL_BG,
            outline=PANEL_OUTLINE,
            width=2
        )

        line1 = f"Pontos mão | Tu {p0_pts}  IA {p1_pts}"
        line2 = f"Partidas   | Tu {self.match_partidas_p0}  IA {self.match_partidas_p1}"

        self.draw_text_shadow(
            x1+16, y1+10,
            line1, TEXT_MAIN,
            ("Consolas", 14, "bold"), "nw"
        )
        self.draw_text_shadow(
            x1+16, y1+32,
            line2, TEXT_MAIN,
            ("Consolas", 14, "bold"), "nw"
        )
        self.draw_text_shadow(
            x1+16, y1+54,
            turn_txt,
            TEXT_WARN if not finished else "#ff4444",
            ("Consolas", 13, "bold"), "nw"
        )

        # mini dica no canto inferior direito
        self.canvas.create_text(
            x2-8, y2-6,
            text=f"Primeiro a {PARTIDAS_TARGET}+ partidas é rei 👑",
            fill=TEXT_SUB,
            font=("Consolas", 10, "italic"),
            anchor="se"
        )

        # Mostrar última jogada de forma discreta (canto superior direito do painel)
        if self.last_play_description:
            self.canvas.create_text(
                x2-8, y1+14,
                text=self.last_play_description,
                fill="#ffffff",
                font=("Consolas", 11, "bold"),
                anchor="ne"
            )

    def draw_round_history_panel(self):
        """
        Painel do lado direito topo:
        - título
        - duas linhas estilo lichess:
          linha "Tu": ganhos de cada mão
          linha "IA": ganhos de cada mão
        """
        x1 = ROUND_HISTORY_X
        y1 = ROUND_HISTORY_Y
        x2 = x1 + ROUND_HISTORY_W
        y2 = y1 + ROUND_HISTORY_H

        self.draw_rounded_rect(
            x1, y1, x2, y2,
            r=12,
            fill=PANEL_BG,
            outline=PANEL_OUTLINE,
            width=2
        )

        self.draw_text_shadow(
            x1+16, y1+8,
            "Histórico de mãos (partidas ganhas)",
            TEXT_MAIN,
            ("Consolas", 12, "bold"),
            "nw"
        )

        bx0 = x1 + 60  # espaço para label "Tu"/"IA"
        by_player = y1 + 32
        by_ai = y1 + 56
        bw = 24
        bh = 20
        pad = 4

        # labels "Tu" / "IA"
        self.canvas.create_text(
            x1+32, by_player+bh/2,
            text="Tu",
            fill=TEXT_MAIN,
            font=("Consolas", 11, "bold"),
            anchor="e"
        )
        self.canvas.create_text(
            x1+32, by_ai+bh/2,
            text="IA",
            fill=TEXT_MAIN,
            font=("Consolas", 11, "bold"),
            anchor="e"
        )

        n = len(self.round_history)

        for i, rh in enumerate(self.round_history):
            p0_gain = rh["p0_gain"]
            p1_gain = rh["p1_gain"]

            cell_x1 = bx0 + i*(bw+pad)
            cell_y1 = by_player
            cell_x2 = cell_x1 + bw
            cell_y2 = cell_y1 + bh

            cell2_x1 = bx0 + i*(bw+pad)
            cell2_y1 = by_ai
            cell2_x2 = cell2_x1 + bw
            cell2_y2 = cell2_y1 + bh

            last = (i == n-1)

            if last:
                fill_p = LAST_HILITE_BG
                fg_p   = LAST_HILITE_FG
                fill_a = LAST_HILITE_BG
                fg_a   = LAST_HILITE_FG
            else:
                fill_p = HUMAN_COLOR if p0_gain > 0 else "#222222"
                fg_p   = "#000000" if p0_gain > 0 else "#777777"
                fill_a = AI_COLOR if p1_gain > 0 else "#222222"
                fg_a   = "#000000" if p1_gain > 0 else "#777777"

            # jogador
            self.canvas.create_rectangle(
                cell_x1, cell_y1, cell_x2, cell_y2,
                fill=fill_p,
                outline="#000000"
            )
            self.canvas.create_text(
                (cell_x1+cell_x2)//2,
                (cell_y1+cell_y2)//2,
                text=str(p0_gain) if p0_gain>0 else "0",
                fill=fg_p,
                font=("Consolas", 11, "bold")
            )

            # IA
            self.canvas.create_rectangle(
                cell2_x1, cell2_y1, cell2_x2, cell2_y2,
                fill=fill_a,
                outline="#000000"
            )
            self.canvas.create_text(
                (cell2_x1+cell2_x2)//2,
                (cell2_y1+cell2_y2)//2,
                text=str(p1_gain) if p1_gain>0 else "0",
                fill=fg_a,
                font=("Consolas", 11, "bold")
            )

    def draw_capture_panel(self, x1, y1, x2, y2, color_outline, title, cards_codes):
        """Painel direito com cartas capturadas apresentadas em leque."""
        r = 20
        self.draw_rounded_rect(
            x1, y1, x2, y2,
            r=r,
            fill="",
            outline=color_outline,
            width=3
        )

        self.canvas.create_text(
            x1 + 10, y1 + 10,
            text=title,
            fill=color_outline,
            font=("Consolas", 12, "bold"),
            anchor="nw"
        )

        show_cards = cards_codes[-20:]
        if not show_cards:
            return

        card_w = int(CARD_W * CAP_SCALE)
        card_h = int(CARD_H * CAP_SCALE)
        fan_dx = max(8, int(card_w * 0.35), CAP_FAN_OFFSET)
        fan_dy = max(12, int(card_h * 0.55))
        usable_width = max(1, (x2 - x1) - card_w - 20)
        max_per_row = max(1, usable_width // fan_dx + 1)

        base_x = x1 + 12
        base_y = y1 + 32

        for idx, code in enumerate(show_cards):
            row = idx // max_per_row
            col = idx % max_per_row
            cx = base_x + col * fan_dx
            cy = base_y + row * fan_dy

            img = self.get_card_image(code, face_down=False, scale=CAP_SCALE)
            self.canvas.create_image(cx, cy, anchor="nw", image=img)

    # ---------------- redraw mesa completa ----------------

    def redraw(self, replay_mode=False):
        self.canvas.delete("all")
        self.click_zones = []

        st = self.current_state
        if not st:
            return

        # painel scoreboard topo-esquerda
        self.draw_scoreboard_panel(st)

        # painel histórico topo-direita
        self.draw_round_history_panel()

        # painéis de capturas à direita
        self.draw_capture_panel(
            CAP_AI_X1, CAP_AI_Y1, CAP_AI_X2, CAP_AI_Y2,
            color_outline=AI_COLOR,
            title="Ganhos IA",
            cards_codes=self.captured_p1
        )
        self.draw_capture_panel(
            CAP_P0_X1, CAP_P0_Y1, CAP_P0_X2, CAP_P0_Y2,
            color_outline=HUMAN_COLOR,
            title="Teus Ganhos",
            cards_codes=self.captured_p0
        )

        # trunfo e deck
        trump = st.get("trump")
        trump_given = st.get("trump_given", False)
        if trump is not None and not trump_given:
            trump_img = self.get_card_image(trump["code"], face_down=False, scale=1.0)
            self.canvas.create_image(TRUMP_X, TRUMP_Y, anchor="nw", image=trump_img)

            self.draw_text_shadow(
                TRUMP_X, TRUMP_Y + CARD_H + 10,
                "Trunfo:",
                TEXT_MAIN,
                ("Consolas", 12, "bold"),
                "nw"
            )
            self.canvas.create_text(
                TRUMP_X, TRUMP_Y + CARD_H + 28,
                text=trump["text"],
                fill=TEXT_MAIN,
                font=("Consolas", 11),
                anchor="nw"
            )
            deck_left = st.get("deck_count")
            if deck_left is not None:
                self.canvas.create_text(
                    TRUMP_X, TRUMP_Y + CARD_H + 45,
                    text=f"Deck: {deck_left}",
                    fill=TEXT_SUB,
                    font=("Consolas", 10),
                    anchor="nw"
                )

        # mão IA (virada para baixo)
        for i, card in enumerate(st.get("p1_hand", [])):
            code = card["code"]
            img = self.get_card_image(code, face_down=True, scale=1.0)
            x = P1_X + i * OFFSET_X
            y = P1_Y
            self.canvas.create_image(x, y, anchor="nw", image=img)

        # trick ao centro
        for i, card in enumerate(st.get("trick", [])):
            code = card["code"]
            img = self.get_card_image(code, face_down=False, scale=1.0)
            x = TRICK_X + i * (CARD_W + 10)
            y = TRICK_Y
            self.canvas.create_image(x, y, anchor="nw", image=img)

        # Fallback robusto: se a vaza foi fechada (trick==[]) mas o motor expõe
        # 'last_trick' completa, anima a recolha aqui mesmo (caso a transição 3->0
        # tenha sido perdida por algum timing)
        lt = st.get("last_trick", [])
        if not st.get("trick") and lt and len(lt) >= 4 and not self._collecting:
            self._collecting = True
            self._collect_started_ms = int(time.time() * 1000)
            winner = st.get("last_trick_winner", st.get("current_player", 0))
            dest_x = P0_X if winner == 0 else P1_X
            dest_y = P0_Y if winner == 0 else P1_Y
            base_x = TRICK_X
            base_y = TRICK_Y

            # desenhar 4 cartas no centro
            temp_ids = []
            for i, card in enumerate(lt):
                code = card.get("code") if isinstance(card, dict) else None
                if not code:
                    continue
                cid = self.draw_single_card_temp(code, base_x + i*(CARD_W+10), base_y, face_down=False)
                temp_ids.append(cid)

            # atualizar capturas
            if winner == 0:
                self.captured_p0.extend([c.get("code") for c in lt if isinstance(c, dict)])
            else:
                self.captured_p1.extend([c.get("code") for c in lt if isinstance(c, dict)])

            # animar em cascata
            remaining = [len(temp_ids)]
            def one_done():
                remaining[0] -= 1
                if remaining[0] <= 0:
                    self._collecting = False
                    self.redraw_final()
            for i, cid in enumerate(temp_ids):
                sx = base_x + i*(CARD_W+10)
                sy = base_y
                self.root.after(i*100, lambda cid_=cid, sx_=sx, sy_=sy: self.animate_move_curve(
                    cid_, sx_, sy_, dest_x, dest_y, ctrl_dx=0, ctrl_dy=-40, duration_ms=450, ease='inout', on_done=one_done
                ))

            # safety: se algo falhar na anim, força finalização após 2.5s
            def safety_finalize():
                if self._collecting and int(time.time()*1000) - self._collect_started_ms > 2500:
                    self._collecting = False
                    self.redraw_final()
            self.root.after(2600, safety_finalize)

        # mão do jogador (clicável)
        for i, card in enumerate(st.get("p0_hand", [])):
            code = card["code"]
            img = self.get_card_image(code, face_down=False, scale=1.0)
            x = P0_X + i * OFFSET_X
            y = P0_Y
            self.canvas.create_image(x, y, anchor="nw", image=img)

            bbox = (x, y, x + CARD_W, y + CARD_H)
            self.click_zones.append({
                "bbox": bbox,
                "card_index": card["index"],
                "code": code,
                "x": x,
                "y": y
            })

        # se não estamos a ver replay manual, deixa a IA jogar se for a vez dela
        if not replay_mode:
            self.maybe_schedule_ai()


# ---------------- main ----------------

def main():
    root = tk.Tk()
    app = BiscaGUI(root)
    root.mainloop()


if __name__ == "__main__":
    main()


