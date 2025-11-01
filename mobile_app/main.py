import os
import sys
from collections import OrderedDict
from functools import partial

os.environ.setdefault("KIVY_NO_CONSOLELOG", "1")
os.environ.setdefault("BISCA_ENGINE_NATIVE", "1")

from kivy.app import App  # noqa: E402
from kivy.clock import Clock  # noqa: E402
from kivy.core.window import Window  # noqa: E402
from kivy.uix.boxlayout import BoxLayout  # noqa: E402
from kivy.uix.button import Button  # noqa: E402
from kivy.uix.gridlayout import GridLayout  # noqa: E402
from kivy.uix.image import Image  # noqa: E402
from kivy.uix.label import Label  # noqa: E402
from kivy.uix.spinner import Spinner  # noqa: E402

ROOT_DIR = os.path.abspath(os.path.dirname(__file__))
ASSETS_DIR = os.path.join(ROOT_DIR, "assets")

sys.path.insert(0, os.path.abspath(os.path.join(ROOT_DIR, "..")))

from gui.engine import BiscaEngine  # noqa: E402


def card_image_path(code: str) -> str:
    fname = f"{code.upper()}.png"
    path = os.path.join(ASSETS_DIR, "cards", fname)
    if os.path.exists(path):
        return path
    return os.path.join(ASSETS_DIR, "cards", "blank.png")


class BiscaMobileApp(App):
    title = "Bisca dos 4"

    def build(self):
        Window.clearcolor = (0.02, 0.24, 0.02, 1)

        self.difficulty_profiles = OrderedDict([
            ("Fácil", {"engine": "alphabeta", "exe": "bisca4.exe", "depth": 4, "nnue": "nnue_ab.bin"}),
            ("Médio", {"engine": "mcts", "exe": "bisca4_mcts.exe", "iterations": 2200, "cpuct": 1.35, "nnue": "nnue_mid.bin"}),
            ("Difícil", {"engine": "mcts", "exe": "bisca4_mcts.exe", "iterations": 4200, "cpuct": 1.40, "nnue": "nnue_hard.bin"}),
        ])

        self.current_profile_name = "Difícil"
        self.engine = None
        self.current_state = None
        self.ai_delay = 0.7

        root = BoxLayout(orientation="vertical", padding=12, spacing=12)

        top_bar = BoxLayout(orientation="horizontal", size_hint_y=None, height="48dp", spacing=12)
        self.score_label = Label(text="", font_size="20sp", bold=True)
        self.deck_label = Label(text="", font_size="16sp")
        self.status_label = Label(text="", font_size="16sp")

        self.difficulty_spinner = Spinner(
            text=self.current_profile_name,
            values=list(self.difficulty_profiles.keys()),
            size_hint=(None, None),
            size=("140dp", "44dp"),
        )
        self.difficulty_spinner.bind(text=self.on_difficulty_change)

        new_game_btn = Button(text="Novo jogo", size_hint=(None, None), size=("140dp", "44dp"))
        new_game_btn.bind(on_release=lambda *_: self.restart_game())

        top_bar.add_widget(self.score_label)
        top_bar.add_widget(self.deck_label)
        top_bar.add_widget(self.status_label)
        top_bar.add_widget(self.difficulty_spinner)
        top_bar.add_widget(new_game_btn)

        self.trick_layout = BoxLayout(orientation="horizontal", spacing=8, size_hint_y=None, height="140dp")
        self.trump_box = BoxLayout(orientation="vertical", size_hint_y=None, height="140dp", width="100dp")
        self.trump_label = Label(text="Trunfo", size_hint_y=None, height="24dp", font_size="16sp")
        self.trump_image = Image(size_hint=(1, 1))
        self.trump_box.add_widget(self.trump_label)
        self.trump_box.add_widget(self.trump_image)

        center_layout = BoxLayout(orientation="horizontal", spacing=12, size_hint_y=None, height="160dp")
        center_layout.add_widget(self.trump_box)
        center_layout.add_widget(self.trick_layout)

        self.ai_hand_layout = BoxLayout(orientation="horizontal", spacing=8, size_hint_y=None, height="120dp")
        self.player_hand_layout = BoxLayout(orientation="horizontal", spacing=8, size_hint_y=None, height="180dp")

        root.add_widget(top_bar)
        root.add_widget(self.ai_hand_layout)
        root.add_widget(center_layout)
        root.add_widget(self.player_hand_layout)

        self.start_engine(self.current_profile_name)
        return root

    # ---------- Engine Handling ----------

    def start_engine(self, profile_name: str):
        if self.engine:
            try:
                self.engine.stop()
            except Exception:
                pass
            self.engine = None

        profile = self.difficulty_profiles[profile_name]

        exe_path = os.path.join(ROOT_DIR, profile.get("exe", "bisca4.exe"))
        nnue_file = profile.get("nnue")
        nnue_path = os.path.join(ASSETS_DIR, "nnue", nnue_file) if nnue_file else ""

        kwargs = {
            "exe_path": exe_path,
            "nnue_path": nnue_path,
            "depth": profile.get("depth", 4),
            "iterations": profile.get("iterations", 2000),
            "cpuct": profile.get("cpuct", 1.41421356),
            "engine_type": profile.get("engine", "alphabeta"),
            "perfect_info": profile.get("perfect_info", False),
        }

        # tenta engine nativo primeiro, fallback para subprocesso
        try:
            self.engine = BiscaEngine(backend="native", **kwargs)
            state = self.engine.start()
            if self.engine.status_message:
                self.status_label.text = self.engine.status_message
        except Exception as exc:
            self.status_label.text = f"Nativo indisponível ({exc}). A usar subprocesso."
            self.engine = BiscaEngine(backend="subprocess", **kwargs)
            state = self.engine.start()

        self.apply_state(state)

    def restart_game(self):
        if not self.engine:
            return
        state = self.engine.new_game()
        self.status_label.text = "Novo jogo iniciado."
        self.apply_state(state)

    # ---------- UI Updates ----------

    def apply_state(self, state: dict):
        self.current_state = state
        if not state:
            return

        self.score_label.text = f"Você: {state.get('score0', 0)}  |  IA: {state.get('score1', 0)}"
        deck_count = state.get("deck_count")
        self.deck_label.text = f"Deck: {deck_count if deck_count is not None else '?'}"

        trump = state.get("trump")
        if trump:
            self.trump_image.source = card_image_path(trump.get("code", "blank"))
        else:
            self.trump_image.source = card_image_path("blank")

        self.render_trick(state.get("trick", []))
        self.render_ai_hand(state.get("p1_hand", []))
        self.render_player_hand(state.get("p0_hand", []), state)

        if state.get("finished"):
            self.status_label.text = "Fim do jogo!"
        else:
            self.schedule_ai_move(state)

    def render_trick(self, cards):
        self.trick_layout.clear_widgets()
        for card in cards:
            img = Image(source=card_image_path(card.get("code", "blank")), size_hint=(None, 1), width=100)
            self.trick_layout.add_widget(img)

    def render_ai_hand(self, cards):
        self.ai_hand_layout.clear_widgets()
        for _ in cards:
            img = Image(source=card_image_path("blank"), size_hint=(None, 1), width=80)
            self.ai_hand_layout.add_widget(img)

    def render_player_hand(self, cards, state):
        self.player_hand_layout.clear_widgets()
        player_turn = not state.get("finished") and state.get("current_player") == 0
        for card in cards:
            btn = Button(
                background_normal=card_image_path(card.get("code", "blank")),
                background_down=card_image_path(card.get("code", "blank")),
                size_hint=(None, 1),
                width=100,
                disabled=not player_turn,
            )
            btn.bind(on_release=partial(self.on_player_card, card.get("index", 0)))
            self.player_hand_layout.add_widget(btn)

    # ---------- Gameplay ----------

    def on_player_card(self, card_index, *_):
        if not self.engine or self.current_state is None:
            return
        if self.current_state.get("finished"):
            return
        if self.current_state.get("current_player") != 0:
            return

        message, state = self.engine.play_card(card_index)
        if message:
            self.status_label.text = message
        self.apply_state(state)

    def schedule_ai_move(self, state):
        if state.get("finished"):
            return
        if state.get("current_player") != 1:
            return
        Clock.schedule_once(self._ai_move, self.ai_delay)

    def _ai_move(self, *_):
        if not self.engine or self.current_state is None:
            return
        if self.current_state.get("finished"):
            return
        if self.current_state.get("current_player") != 1:
            return

        idx, info = self.engine.bestmove()
        if idx is None:
            return
        message, state = self.engine.play_card(idx)
        self.status_label.text = message or info or "IA jogou."
        self.apply_state(state)

    def on_difficulty_change(self, spinner, text):
        if text == self.current_profile_name:
            return
        self.current_profile_name = text
        self.start_engine(text)

    def on_stop(self):
        if self.engine:
            try:
                self.engine.stop()
            except Exception:
                pass


if __name__ == "__main__":
    BiscaMobileApp().run()
