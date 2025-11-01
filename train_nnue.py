import struct
import argparse
import torch
import torch.nn as nn
import torch.optim as optim
import numpy as np

# -------------------------------------------------
# Constantes da rede (têm de bater com o motor C++)
# Agora: 178 inputs, 2 camadas ocultas: 64 e 32
# -------------------------------------------------
INPUT_SIZE = 178
H1 = 64
H2 = 32

# -------------------------------------------------
# Ler dataset.bin
#
# Formato gravado pelo motor C++ (saveSamples):
#
#   uint32 nSamples
#   para cada sample:
#       uint32 featLen
#       featLen * float32   (features)
#       float32             (outcome)
#
# Notas:
# - featLen agora deve ser 178
# - outcome é (score0 - score1) da perspetiva do jogador que IA estava a jogar
#   naquele estado.
# - Vamos aplicar um fator lambda_scale opcional (tal como combinámos
#   quando falámos de "lambda estilo stockfish"):
#   target = outcome * lambda_scale
# -------------------------------------------------
def load_dataset(path, lambda_scale=1.0):
    with open(path, "rb") as f:
        raw = f.read()

    off = 0

    def read_u32():
        nonlocal off
        val = struct.unpack_from("<I", raw, off)[0]
        off += 4
        return val

    def read_f32():
        nonlocal off
        val = struct.unpack_from("<f", raw, off)[0]
        off += 4
        return val

    n = read_u32()
    feats = []
    outs = []

    for _ in range(n):
        flen = read_u32()
        vec = [read_f32() for _ in range(flen)]
        outcome = read_f32()
        feats.append(vec)
        outs.append(outcome)

    X = torch.tensor(feats, dtype=torch.float32)              # [N, featLen]
    y = torch.tensor(outs, dtype=torch.float32).unsqueeze(1)  # [N, 1]

    # sanity check
    if X.shape[1] != INPUT_SIZE:
        raise ValueError(
            f"O dataset tem {X.shape[1]} features por sample "
            f"mas o motor espera INPUT_SIZE={INPUT_SIZE}. "
            f"Isto normalmente acontece se geraste dataset "
            f"com uma versão antiga das features."
        )

    # aplica lambda_scale aos targets
    y = y * float(lambda_scale)

    return X, y

# -------------------------------------------------
# Modelo NNUE equivalente ao motor C++
#
# fc1: Linear(INPUT_SIZE -> HIDDEN_SIZE), ReLU
# fc2: Linear(HIDDEN_SIZE -> 1)
# -------------------------------------------------
class NNUEModel(nn.Module):
    def __init__(self, input_size, h1, h2):
        super().__init__()
        self.fc1 = nn.Linear(input_size, h1)
        self.fc2 = nn.Linear(h1, h2)
        self.fc3 = nn.Linear(h2, 1)

    def forward(self, x):
        x = torch.relu(self.fc1(x))
        x = torch.relu(self.fc2(x))
        x = self.fc3(x)
        return x

# -------------------------------------------------
# Carregar pesos no formato binário do motor C++
#
# Formato C++ (saveWeights):
#   int inputSize
#   int hiddenSize
#   w1[hiddenSize*inputSize] float32
#   b1[hiddenSize]           float32
#   w2[hiddenSize]           float32
#   b2                       float32
#
# Isto corresponde a:
#   fc1.weight: [hidden,input]
#   fc1.bias:   [hidden]
#   fc2.weight: [1,hidden]
#   fc2.bias:   [1]
#
# Se quiseres continuar treino de uma NNUE já existente,
# passas esse ficheiro via --init-weights.
# -------------------------------------------------
def load_weights_into_model(model, path):
    with open(path, "rb") as f:
        raw = f.read()

    off = 0

    def read_i32():
        nonlocal off
        val = struct.unpack_from("<i", raw, off)[0]
        off += 4
        return val

    inSz = read_i32()
    h1 = read_i32()

    # tentar ler h2 (novo formato). se falhar, antigo
    try:
        h2 = read_i32()
        new_fmt = True
    except Exception:
        new_fmt = False

    if inSz != INPUT_SIZE:
        raise ValueError(f"INPUT_SIZE incompatível: ficheiro {inSz}, esperado {INPUT_SIZE}")

    with torch.no_grad():
        if new_fmt:
            count_w1 = h1 * inSz
            w1 = np.frombuffer(raw, dtype=np.float32, count=count_w1, offset=off); off += count_w1*4
            b1 = np.frombuffer(raw, dtype=np.float32, count=h1, offset=off); off += h1*4
            count_w2 = h2 * h1
            w2 = np.frombuffer(raw, dtype=np.float32, count=count_w2, offset=off); off += count_w2*4
            b2 = np.frombuffer(raw, dtype=np.float32, count=h2, offset=off); off += h2*4
            w3 = np.frombuffer(raw, dtype=np.float32, count=h2, offset=off); off += h2*4
            b3 = np.frombuffer(raw, dtype=np.float32, count=1, offset=off); off += 4

            if h1 != H1 or h2 != H2:
                raise ValueError(f"Dimensões NNUE incompatíveis: ficheiro {inSz}->{h1}->{h2}, esperado {INPUT_SIZE}->{H1}->{H2}")
            model.fc1.weight.copy_(torch.tensor(w1.reshape(h1, inSz)))
            model.fc1.bias.copy_(torch.tensor(b1))
            model.fc2.weight.copy_(torch.tensor(w2.reshape(h2, h1)))
            model.fc2.bias.copy_(torch.tensor(b2))
            model.fc3.weight.copy_(torch.tensor(w3.reshape(1, h2)))
            model.fc3.bias.copy_(torch.tensor(b3))
        else:
            hidSz = h1
            count_w1 = hidSz * inSz
            w1 = np.frombuffer(raw, dtype=np.float32, count=count_w1, offset=off); off += count_w1*4
            b1 = np.frombuffer(raw, dtype=np.float32, count=hidSz, offset=off); off += hidSz*4
            w2 = np.frombuffer(raw, dtype=np.float32, count=hidSz, offset=off); off += hidSz*4
            b2 = np.frombuffer(raw, dtype=np.float32, count=1, offset=off); off += 4

            model.fc1.weight.copy_(torch.tensor(w1.reshape(hidSz, inSz)))
            model.fc1.bias.copy_(torch.tensor(b1))
            # identidade parcial em fc2 e projecção antiga em fc3
            model.fc2.weight.zero_(); model.fc2.bias.zero_()
            rows = min(H2, hidSz)
            model.fc2.weight[:rows, :rows].copy_(torch.eye(rows))
            model.fc3.weight.zero_(); model.fc3.bias.copy_(torch.tensor(b2))
            model.fc3.weight[0, :hidSz].copy_(torch.tensor(w2))

# -------------------------------------------------
# Guardar pesos treinados de volta para .bin
# compatível com o motor C++
# -------------------------------------------------
def save_model_weights(model, path):
    fc1_w = model.fc1.weight.detach().cpu().numpy()  # (H1,input)
    fc1_b = model.fc1.bias.detach().cpu().numpy()    # (H1,)
    fc2_w = model.fc2.weight.detach().cpu().numpy()  # (H2,H1)
    fc2_b = model.fc2.bias.detach().cpu().numpy()    # (H2,)
    fc3_w = model.fc3.weight.detach().cpu().numpy()  # (1,H2)
    fc3_b = model.fc3.bias.detach().cpu().numpy()    # (1,)

    with open(path, "wb") as f:
        f.write(struct.pack("<i", INPUT_SIZE))
        f.write(struct.pack("<i", H1))
        f.write(struct.pack("<i", H2))
        f.write(fc1_w.astype("float32").tobytes(order="C"))
        f.write(fc1_b.astype("float32").tobytes(order="C"))
        f.write(fc2_w.astype("float32").tobytes(order="C"))
        f.write(fc2_b.astype("float32").tobytes(order="C"))
        f.write(fc3_w.reshape(-1).astype("float32").tobytes(order="C"))
        f.write(fc3_b.astype("float32").tobytes(order="C"))

# -------------------------------------------------
# Função de treino
#
# - epochs configurável
# - learning rate configurável
# - weight_decay (=L2 regularization) opcional
# - (futuro: podes pôr batch training; agora é full-batch para simplicidade)
# -------------------------------------------------
def train_model(model, X, y, epochs=200, lr=1e-3, weight_decay=0.0, batch_size=8192):
    opt = optim.AdamW(model.parameters(), lr=lr, weight_decay=weight_decay)
    loss_fn = nn.SmoothL1Loss()

    N = X.shape[0]
    idx = torch.arange(N)

    for epoch in range(epochs):
        perm = idx[torch.randperm(N)]
        Xs = X[perm]
        ys = y[perm]

        total = 0.0
        steps = 0
        device = next(model.parameters()).device
        for i in range(0, N, batch_size):
            xb = Xs[i:i+batch_size].to(device)
            yb = ys[i:i+batch_size].to(device)
            opt.zero_grad()
            pred = model(xb)
            loss = loss_fn(pred, yb)
            loss.backward()
            torch.nn.utils.clip_grad_norm_(model.parameters(), 1.0)
            opt.step()
            total += loss.item()
            steps += 1
        if (epoch + 1) % 10 == 0 or epoch == 1:
            print(f"epoch {epoch+1:4d}  loss={total/steps:.6f}")

# -------------------------------------------------
# main
# -------------------------------------------------
def main():
    ap = argparse.ArgumentParser()

    ap.add_argument(
        "--dataset",
        default="dataset.bin",
        help="dataset gerado pelo motor (--mode selfplay)"
    )
    ap.add_argument(
        "--out-weights",
        default="nnue_trained.bin",
        help="ficheiro .bin de saida para pesos treinados (compatível com C++)"
    )
    ap.add_argument(
        "--init-weights",
        default=None,
        help="ficheiro .bin existente para continuar treino (mesma dimensão)"
    )
    ap.add_argument(
        "--epochs",
        type=int,
        default=200,
        help="numero de epocas de treino"
    )
    ap.add_argument(
        "--lr",
        type=float,
        default=1e-3,
        help="learning rate do Adam"
    )
    ap.add_argument(
        "--lambda-scale",
        type=float,
        default=1.0,
        help="fator lambda que escala o target outcome (ex: 0.01 para normalizar)"
    )
    ap.add_argument(
        "--batch-size",
        type=int,
        default=8192,
        help="tamanho do minibatch para treino"
    )
    ap.add_argument(
        "--l2",
        type=float,
        default=0.0,
        help="weight decay (L2 regularization) para Adam"
    )
    ap.add_argument(
        "--device",
        default="auto",
        choices=["auto", "cpu", "cuda"],
        help="dispositivo para treino (auto/cpu/cuda)"
    )

    args = ap.parse_args()

    print("Loading dataset:", args.dataset)
    X, y = load_dataset(args.dataset, lambda_scale=args.lambda_scale)
    print("Dataset shape:", X.shape, y.shape)
    # X: [N,178], y: [N,1]

    # criar modelo
    model = NNUEModel(INPUT_SIZE, H1, H2)
    # escolher device
    if args.device == "cuda" or (args.device == "auto" and torch.cuda.is_available()):
        device = torch.device("cuda")
    else:
        device = torch.device("cpu")
    model.to(device)

    # continuar treino a partir de rede existente?
    if args.init_weights is not None:
        print("Loading initial weights from:", args.init_weights)
        load_weights_into_model(model, args.init_weights)

    print("Training...")
    train_model(
        model,
        X, y,
        epochs=args.epochs,
        lr=args.lr,
        weight_decay=args.l2,
        batch_size=args.batch_size
    )

    print("Saving weights to:", args.out_weights)
    save_model_weights(model, args.out_weights)

    print("Done.")

if __name__ == "__main__":
    main()
