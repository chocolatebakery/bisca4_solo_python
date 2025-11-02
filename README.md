# 🃏 Bisca4

**Bisca4** is a modern C++/NNUE implementation of the Portuguese card game *Bisca dos 4* — a fast, strategic two-player variant of *Sueca*.  
This repository includes a self-play engine, MCTS and Alpha-Beta backends, and Python scripts for NNUE training and reinforcement learning.

---

## 🎮 Game Rules

Bisca4 is played with **two players** and a **reduced 40-card deck** (7s, 8s, and 9s removed).

- Each player starts with **4 cards**.  
- The top card of the deck defines the **trump suit**.  
- Players alternate turns: Player 1 plays, Player 2 responds, Player 1 plays again, and Player 2 ends the hand.  
- After each hand, both players **draw two new cards**.  
- The **winner of the previous hand leads** the next.

### Card Values

| Card | Points |
|------|---------|
| Queen | 2 |
| Jack | 3 |
| King | 4 |
| 10 | 10 |
| Ace | 11 |

Cards rank as follows:  
`2 < 3 < 4 < 5 < 6 < 10 < Queen < Jack < King < Ace`

### Trump Rules

- The trump suit beats any other suit, unless a **higher trump** is played.
- “Renúncia” (renounce) is allowed: even if you hold the led suit, you may play another card.
- Example:  
  If trumps are ♥, you play 10♦, your opponent plays 2♠, then you play Ace♦ and they play Queen♠ → you win the hand (♦ is the leading suit).

---

## ⚙️ Building

### Prerequisites
- CMake ≥ 3.20  
- A C++17 compiler  
- Python 3 (for NNUE training)  

### Build Instructions
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

To build individual executables:
```bash
cmake --build build --target bisca4 --config Release
cmake --build build --target bisca4_mcts --config Release
cmake --build build --target bisca4_match --config Release
```

---

## 🧠 Engine Modes

### 1. Self-Play (data generation)
```bash
bisca4.exe --mode selfplay --games 500 --depth 3 --nnue nnue_trained.bin
```
If the NNUE file fails to load, a fallback `nnue_random.bin` is created automatically.  
This mode produces a `dataset.bin` file for NNUE training.

### 2. Engine Mode (UCI-like loop)
```bash
bisca4.exe --mode engine
```
Example session:
```
bisca4> newgame
bisca4> show
bisca4> bestmove
bisca4> play 2
bisca4> quit
```
This mode can be connected to a **future C# GUI**, communicating through `stdin`/`stdout`.

### 3. Match Mode (engine vs engine)
```bash
bisca4_match --engine1 ab --nnue1 nnue_iter47.bin --depth1 6              --engine2 mcts --iterations2 6000 --cpuct2 1.4 --games 200
```

---

## 🧩 NNUE Training

### Basic training from scratch
```bash
python train_nnue.py --dataset dataset.bin --out-weights nnue_trained.bin --epochs 200
```

### With λ regularization
```bash
python train_nnue.py --dataset dataset.bin --out-weights nnue_trained.bin --lambda-scale 0.01 --epochs 400
```

### Continuing training
```bash
python train_nnue.py --dataset dataset.bin --init-weights nnue_trained.bin --out-weights nnue_trained_v2.bin --epochs 5000 --lr 5e-4
```

You can chain training sessions to refine the network:
```
v2 → v3 → v4
```

---

## 🌲 MCTS Mode

New executable: `bisca4_mcts`

### Engine mode
```bash
bisca4_mcts --mode engine --iterations 3000 --cpuct 1.2 --info partial
```

### Self-play mode
```bash
bisca4_mcts --mode selfplay --games 1000 --iterations 2000 --cpuct 1.4              --info perfect --threads 8 --dataset dataset_mcts.bin
```

`--depth` is accepted as an alias for `--iterations`.

---

## 🧪 Match Evaluation

Example:
```bash
bisca4_match --engine1 ab --nnue1 nnue_iter16.bin --depth1 9 ^
             --engine2 mcts --nnue2 nnue_iter16.bin --iterations2 4200 --cpuct2 1.35 ^
             --name1 "AB" --name2 "MCTS" --games 400
```

---

## 🧭 MCTS Parameters

| Parameter | Description |
|------------|-------------|
| `--iterations` | Number of playouts per move. 1–2k for testing, 4–8k for strong play. |
| `--cpuct` | Exploration constant (UCT formula). Default = 1.4. Lower = exploit, higher = explore. |

---

## 🧬 Reinforcement Learning Workflow

1. Run self-play to generate datasets  
2. Train NNUE using `train_nnue.py`  
3. Evaluate new weights using `bisca4_match`  
4. Adjust parameters gradually:  
   - Increase `--iterations` as the network improves  
   - Tune `--lambda-scale`, `--lr`, and `--epochs`  
5. Archive each iteration (`nnue_iterX.bin`) to benchmark versions.

---

## 🖼️ Assets

Playing card images from:  
🎨 [https://opengameart.org/content/playing-cards-vector-png](https://opengameart.org/content/playing-cards-vector-png)  
Created by **Kenney** – [kenney.nl](https://kenney.nl)  
Licensed under **CC0 (Public Domain)**

---

## ⚖️ License

Code licensed under the **BSD 3-Clause License**.  
See the [LICENSE](./LICENSE) file for full terms.

Card assets: **CC0 (Public Domain)**.

---

## ❤️ Credits

- C++ engine and NNUE logic by chocolatebakery
- Card graphics by [Kenney.nl](https://kenney.nl)  
- Inspired by the traditional Portuguese game *Bisca dos 4*  
- Made with 🧠, ☕, and a few lucky hands of cards.

---

> “Sometimes you play to win, sometimes you play to learn — but the best games make you smile.”
