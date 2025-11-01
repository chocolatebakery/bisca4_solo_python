# Compilar os motores para Android

Este diretório contém um script de apoio para gerar builds *arm64-v8a* dos binários `bisca4` e `bisca4_mcts` usando o Android NDK.

## Pré-requisitos

1. Instale o [Android NDK](https://developer.android.com/ndk) e garanta que a variável de ambiente `ANDROID_NDK_HOME` aponta para o diretório de raiz do NDK.
2. Instale o CMake (>= 3.10) e um gerador suportado (por omissão o script usa `ninja`).
3. Copie os ficheiros NNUE (`*.bin`) para um local acessível no dispositivo Android. Os executáveis procuram automaticamente no diretório atual, em `gui/` e em `assets/`.

## Passos

```bash
# No diretório raiz do repositório
export ANDROID_NDK_HOME=/caminho/para/android-ndk
./android/build_android.sh
```

Pode personalizar o ABI, plataforma e diretório de build através das variáveis de ambiente:

- `ANDROID_ABI` (por omissão `arm64-v8a`)
- `ANDROID_PLATFORM` (por omissão `android-24`)
- `BUILD_DIR` (por omissão `build/android` dentro do repositório)

Os binários resultantes ficam em `build/android/` (ou no diretório configurado).

## Utilização dos ficheiros NNUE

Ao arrancar, os binários tentam carregar a rede NNUE pelo nome recebido em `--nnue`. Caso o caminho indicado não exista, são testadas automaticamente as pastas `gui/` e `assets/`. Pode também definir a variável de ambiente `NNUE_DIR` ou `NNUE_ASSETS_DIR` para indicar explicitamente o diretório onde se encontram os `.bin` no dispositivo.
