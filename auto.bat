@echo off
REM ============================================================
REM Bisca4 self-learning loop (iterative RL style)
REM Cada geração joga contra si própria mais recente.
REM Epochs fixos = 400. Dataset limpo a cada iteração.
REM ============================================================

REM --- AJUSTA ESTA LINHA PARA O TEU PYTHON VENV ---
REM Exemplo:
REM set PYTHON=C:\Users\RYZEN\Documents\bisca4\venv\Scripts\python.exe
set PYTHON=python.exe

REM Motor e script (assumimos que este .bat está na mesma pasta)
set ENGINE=bisca4.exe
set TRAIN_SCRIPT=train_nnue.py
for %%F in ("%ENGINE%") do set ENGINE_BASENAME=%%~nxF

REM Hiperparâmetros base
set DEPTH=8
set MCTS_ITER=2000
set MCTS_CPUCT=1.40
set GAMES_START=200
set MAX_GAMES=4000
set GROW_GAMES=2.0

REM Treino fixo
set EPOCHS_FIXED=4000
set LR=0.001
set LAMBDA_SCALE=0.01
set L2_REG=0.0001

REM Diretório base
set BASEDIR=%~dp0
cd /d "%BASEDIR%"

REM ------------------------------------------------------------
REM ESTADO DO LOOP
REM ------------------------------------------------------------
REM MUITO IMPORTANTE:
REM Tens de garantir que já existe um ficheiro nnue_iter0.bin
REM antes de correres isto (rede inicial).
REM ------------------------------------------------------------

set /a ITER=0
set GAMES=%GAMES_START%
set THREADS=%NUMBER_OF_PROCESSORS%

echo.
echo ==========================================
echo  LOOP DE TREINO DA BISCA4 (RL SELF-PLAY)
echo  (Ctrl+C para parar)
echo ==========================================
echo.

:mainloop
echo ------------------------------------------
echo Iteracao %ITER%
echo Jogos (selfplay)  = %GAMES%
echo Epochs (treino)   = %EPOCHS_FIXED%
echo ------------------------------------------

REM ------------------------------------------------------------
REM 1. Escolher qual rede usar nesta iteracao
REM ------------------------------------------------------------
REM CURRENT_NET = nnue_iter%ITER%.bin
set CURRENT_NET=nnue_iter%ITER%.bin

if not exist "%CURRENT_NET%" (
    echo ERRO: nao encontrei "%CURRENT_NET%".
    echo Tens de ter pelo menos nnue_iter0.bin antes de arrancar.
    goto end
)

REM Limitar número de jogos
if %GAMES% GTR %MAX_GAMES% set GAMES=%MAX_GAMES%

REM Construir nomes de output desta iteracao (modo parcial apenas)
set MODE_LABEL=partial
set INFO_VALUE=partial
set DATASET=dataset_iter%ITER%_%MODE_LABEL%.bin
set REPORT=selfplay_report_iter%ITER%_%MODE_LABEL%.txt
set TRAINED_NET=nnue_trained_iter%ITER%_%MODE_LABEL%.bin
set TEMP_WEIGHTS=temp_unused_%MODE_LABEL%.bin
for %%F in ("%ENGINE%") do set ENGINE_BASENAME=%%~nxF

echo.
echo [1/4] SELF-PLAY (%MODE_LABEL%) com "%CURRENT_NET%" -> gerar %GAMES% jogos
echo Dataset destino: %DATASET%
if /I "%ENGINE_BASENAME%"=="bisca4_mcts.exe" (
    echo Iteracoes MCTS   = %MCTS_ITER%
    echo CPUCT            = %MCTS_CPUCT%
) else (
    echo Profundidade     = %DEPTH%
)
echo.

if /I "%ENGINE_BASENAME%"=="bisca4_mcts.exe" (
    "%ENGINE%" --mode selfplay --games %GAMES% --iterations %MCTS_ITER% --cpuct %MCTS_CPUCT% --threads %THREADS% --dataset "%DATASET%" --info %INFO_VALUE% --nnue "%CURRENT_NET%"
) else (
    "%ENGINE%" --mode selfplay --nnue "%CURRENT_NET%" --games %GAMES% --depth %DEPTH% --threads %THREADS% --dataset "%DATASET%" --out-weights "%TEMP_WEIGHTS%" --info %INFO_VALUE%
)
if errorlevel 1 (
    echo ERRO: selfplay falhou. A sair...
    goto end
)

if not exist "%DATASET%" (
    echo ERRO: selfplay nao criou "%DATASET%" -- verifica main.cpp.
    goto end
)

if exist selfplay_report.txt (
    copy /Y selfplay_report.txt "%REPORT%" >nul
)

echo.
echo [2/4] TREINO PyTorch
echo Dataset: %DATASET%
echo Rede inicial: %CURRENT_NET%
echo Rede saida:   %TRAINED_NET%
echo Epochs: %EPOCHS_FIXED%
echo.

REM L2 só a partir da iteração 1+
set L2_NOW=0.0
if %ITER% GEQ 1 (
    set L2_NOW=%L2_REG%
)

"%PYTHON%" "%TRAIN_SCRIPT%" ^
    --dataset "%DATASET%" ^
    --init-weights "%CURRENT_NET%" ^
    --out-weights "%TRAINED_NET%" ^
    --epochs %EPOCHS_FIXED% ^
    --lr %LR% ^
    --lambda-scale %LAMBDA_SCALE% ^
    --l2 %L2_NOW%

if errorlevel 1 (
    echo ERRO: treino falhou. A sair sem promover nova rede.
    goto cleanup_and_continue
)

if not exist "%TRAINED_NET%" (
    echo Aviso: treino terminou mas nao gerou "%TRAINED_NET%".
    echo A sair sem promover nova rede.
    goto cleanup_and_continue
)

echo.
echo [3/4] PROMOVER REDE NOVA
REM A rede treinada nesta iteracao passa a ser a rede da proxima iteracao.
REM Proxima iteracao = ITER+1 => nnue_iter%ITERPLUS%.bin
set /a ITERPLUS=%ITER%+1
set NEXT_NET=nnue_iter%ITERPLUS%.bin

copy /Y "%TRAINED_NET%" "%NEXT_NET%" >nul
echo Nova rede ativa para a proxima iteracao: %NEXT_NET%

:cleanup_and_continue
echo.
echo [4/4] LIMPEZA: apagar dataset bruto "%DATASET%"
if exist "%DATASET%" del /f /q "%DATASET%"
if exist "%TEMP_WEIGHTS%" del /f /q "%TEMP_WEIGHTS%"

echo Iteracao %ITER% concluida.
echo.

REM ------------------------------------------------------------
REM 5. Preparar próxima iteracao
REM ------------------------------------------------------------

REM Crescer número de jogos até ao máximo
for /f "usebackq tokens=*" %%A in (`
    powershell -NoProfile -Command "$val=[double](%GAMES%) * %GROW_GAMES%; if ($val -gt %MAX_GAMES%) {$val=%MAX_GAMES%}; [math]::Ceiling($val)"
`) do (
    set GAMES=%%A
)
set /a ITER=%ITER%+1

echo ------------------------------------------
echo Proxima iteracao: ITER=%ITER% GAMES=%GAMES%
echo   (a rede usada sera nnue_iter%ITER%.bin)
echo (Ctrl+C para parar)
echo ------------------------------------------
echo.

goto mainloop

:end
echo Fim do treino.
pause
