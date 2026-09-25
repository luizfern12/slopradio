# Changelog — Modificações no LaraRadio

Todas as alterações entre o release original `gutierre69/lararadio`
e este fork (`brdelphus/lararadio`).

Original: https://github.com/gutierre69/lararadio
Fork: https://github.com/brdelphus/lararadio

---

## [Não lançado]

### Adicionado

#### `mainwindow.h` / `mainwindow.cpp`
- **Arrastar e soltar arquivos na playlist**: agora é possível arrastar
  arquivos de áudio para a playlist (`audio_list`) a partir do
  gerenciador de arquivos do sistema — ou dos navegadores de
  arquivos/vinhetas internos (arrastar habilitado nos dois `QTreeView`s).
  Arquivos viram itens `music`, pastas viram itens `folder-music`,
  igual ao comportamento de "adicionar pelo navegador de arquivos".
- **`addFileToPlaylist()`**: a lógica de metadados TagLib + duração que
  estava duplicada nos handlers de duplo clique foi extraída para um
  único helper, agora compartilhado pelo arrastar e soltar e pelos dois
  navegadores de arquivos.
- **`dragEnterEvent()` / `dropEvent()`**: solturas com URLs de arquivos
  são aceitas em qualquer lugar da janela e adicionadas à playlist. O
  alvo do soltar não depende de `widgetAt()`/mapeamento de posição
  (imprevisível durante drags reais e após redimensionar a janela), então
  o arrastar e soltar funciona em qualquer tamanho de janela — inclusive
  depois de redimensionar.
- **Tecla Del na playlist**: pressionar `Del` com a playlist
  (`audio_list`) em foco remove o item selecionado — igual ao botão de
  remover. Ignorada quando os navegadores de arquivos estão em foco.
- **Multi seleção na playlist**: modo `ExtendedSelection` —
  `Ctrl`+clique em linhas individuais, `Shift`+clique para intervalos.
  O botão de remover e o atalho `Del` agora removem **todas** as linhas
  selecionadas de uma vez (sem seleção, removem o item atual — igual ao
  comportamento antigo de item único).
- **Ponteiros de reprodução sobrevivem à remoção**:
  `current_play`/`next_play` são decrementados para cada linha removida
  acima deles, então remover linhas acima da faixa em reprodução
  continua apontando para a mesma faixa (antes só ajustava o limite,
  o que podia pular faixas após uma remoção).

#### `configdialog.ui` / `configdialog.cpp`
- **Configurações de saída de som (aba Saídas)**: a aba de espaço
  vazio agora permite escolher o dispositivo de saída de áudio —
  "Padrão do sistema" ou qualquer dispositivo de
  `QMediaDevices::audioOutputs()`. A lista se atualiza em tempo real
  com o dialogo aberto (plug/remove de dispositivos) e a escolha é
  salva em `audio/outputDevice` (por id do dispositivo).

#### `audioplayer.h` / `audioplayer.cpp` / `buttonhole.h` / `buttonhole.cpp`
- **`AudioPlayer::configuredAudioDevice()`**: resolve o id salvo do
  dispositivo contra os dispositivos conectados, voltando ao padrão do
  sistema quando nada está salvo ou o dispositivo sumiu. Todos os
  players aplicam na construção — os dois players de crossfade e os
  botões da botoeira (`ButtonHole`).

#### `mainwindow.h` / `mainwindow.cpp`
- **`applyAudioOutputDevice()`**: aplica o dispositivo configurado em
  todas as saídas ativas (players de crossfade, player da locução de
  hora, botoeira) na inicialização e de novo após fechar o dialogo de
  configurações — apenas quando o dispositivo mudou de verdade, para
  não interromper a reprodução ao abrir o dialogo.

#### `cuewindow.ui` / `cuewindow.h` / `cuewindow.cpp`
- **Mini player de pré escuta (Pré Escuta)**: a ação "Pré Escuta" do
  menu de contexto da playlist, antes desabilitada, agora abre uma
  pequena janela de preview sempre visível (always-on-top) com
  transporte próprio — barra de posição com tempos `mm:ss`, play/pause,
  stop e um slider de volume dos fones. Toca a linha clicada (arquivos
  `music`/`jingle`, um arquivo aleatório de pastas, o áudio da hora
  certa) por um `AudioPlayer` dedicado que nunca se conecta ao VU
  meter nem ao watchdog de silêncio, e nunca para sozinho — apenas os
  próprios controles da janela ou fechá-la encerram a pré escuta.
  Clicar em outra linha usa a mesma janela.

#### `configdialog.ui` / `configdialog.cpp`
- **Dispositivo de fones (aba Saídas)**: um segundo combo
  (`Dispositivo de fones (cue)`, salvo em `audio/cueDevice`) roteia as
  pré escutas para um dispositivo de fones dedicado; a primeira opção
  (*Usar saída principal*) segue a saída principal. Os dois combos se
  atualizam juntos ao plug/remove de dispositivos.

#### `audioplayer.h` / `audioplayer.cpp`
- **`configuredCueDevice()` / `Pause()`**: resolve o dispositivo de
  cue salvo com fallback para a saída principal quando ausente ou
  removido, e adiciona o estado de pausa para a janela de pré escuta.
  O player de cue mantém `maxVolume` sincronizado com o slider de
  volume para o timer de fade compartilhado não mexer no nível da
  pré escuta.

### Alterado

#### `mainwindow.ui` / `mainwindow.h` / `mainwindow.cpp`
- **Janela redimensionável**: a janela era de tamanho fixo
  (`setFixedSize` + `sizePolicy Fixed`). Agora pode ser redimensionada
  livremente (mínimo 800×480). Como a UI usa posicionamento absoluto,
  o `resizeEvent()` agora escala todos os widgets proporcionalmente ao
  tamanho de design (1048×622) — o layout mantém as proporções exatas
  em qualquer tamanho de janela, incluindo os VU meters e os botões da
  botoeira criados em código.

#### `configdialog.ui`
- **Dialogo de configurações com abas**: o dialogo agora usa um
  `QTabWidget` com a barra de abas no **lado esquerdo**
  (`TabPosition::West`), dividindo a antiga tela única em quatro
  seções: **Fade** (spinboxes de tempo de fade + opções de fade ao
  parar/falar), **Caminhos** (os três diretórios), **Saídas** (espaço
  vazio para futuras configurações de saída) e **Comportamento**
  (opções do relógio). O dialogo agora usa layouts reais em vez de
  posicionamento absoluto.

---

## [1.0.5] — 2026-07-27 — Modificações sobre o original 1.0.4

### Alterado

#### `audioplayer.h` / `audioplayer.cpp`
- **Herança**: `AudioPlayer` mudou de subclasse de `QMediaPlayer` para
  subclasse de `QObject` contendo `QMediaPlayer *player` como membro
  (composição sobre herança). Desacopla o backend de áudio da API
  pública e permite gerenciamento de ciclo de vida mais flexível.
- **Volume**: volume padrão do `QAudioOutput` mudou de `0` para `1.0`
  (QMediaPlayer agora é a fonte de áudio real)
- **Transcodificação MP3**: adicionado `transcodeIfNeeded()` — converte
  MP3s com album art embedado para WAV temporário via `ffmpeg -vn`
  antes da reprodução. Previne o crash do decoder `mp3float` que
  ocorria após várias músicas com album art.
- **Tratamento de erro**: conectado `QMediaPlayer::errorOccurred` →
  `onPlayerError()` que emite o sinal `mediaError()`
- **Detecção de fim de faixa**: conectado
  `QMediaPlayer::mediaStatusChanged` → emite `playbackFinished()`
  em `EndOfMedia`
- **Destrutor**: destrutor explícito para limpar `player` e `audioOutput`
- **Helpers inline**: `getPosition()`, `getDuration()`, `remainingTime()`,
  `isPlaying()`, `isPaused()`, `isStopped()`, `getVolume()`, `setVolume()`
  simplificados para implementações inline de uma linha
- **`isValidMediaFile()`**: método estático usando `ffprobe` para
  validar arquivos de áudio antes de enfileirar
- **`hasError()`**: flag pública de estado de erro

#### `main.cpp`
- **Crash handler**: adicionado `crashHandler()` para `SIGSEGV`,
  `SIGABRT`, `SIGFPE` — imprime mensagem descritiva no stderr e
  sai com código `128 + signal`. Previne crashes silenciosos do
  decoder FFmpeg.
- **Tema escuro**: ativada a palette do tema escuro Fusion (estava
  comentada). Fundo `#303030`, base `#242424`, texto `#dcdcdc`,
  destaque `#55aaff`. Usa GTK3 quando disponível.
- **Splash screen removido**: o aplicativo não espera mais pelo atraso
  fixo de 2 segundos do splash (`QSplashScreen` + timer de "simular
  trabalho"). A janela principal aparece assim que a inicialização
  termina — inicialização mais rápida (≈2 s economizados). Os assets
  `splash-*.png` foram removidos do `resources.qrc`.

#### `mainwindow.h` / `mainwindow.cpp`
- **`skipToNext()`**: slot público — reseta ambos players, avança
  `current_play` e chama `next()`. Usado quando uma faixa dá erro.
- **`checkAdvanceTrack()`**: slot público — avança a playlist quando
  `playbackFinished` é emitido e ambos os players estão parados.
  Ponto único de avanço da playlist.
- **Recuperação de erro**: sinal `mediaError` de ambos `AudioPlayer`
  conectado a `skipToNext()` — auto-avança em erros do decoder
- **Avanço da playlist**: sinal `playbackFinished` de ambos os players
  conectado a `checkAdvanceTrack()`
- **Hora certa**: verifica `QFile::exists()` antes de tocar o arquivo
  de hora. Se não existir, loga warning e pula sem ativar
  `SayingTimer` — previne deadlock da playlist.
- **Timeplayer error handler**: conectado `QMediaPlayer::errorOccurred`
  no timeplayer — limpa `SayingTimer` e avança em caso de erro
- **Removido avanço duplicado do `flash()`**: o timer de 500ms `flash()`
  não avança mais a playlist. O avanço é feito exclusivamente por
  `checkAdvanceTrack()` via `playbackFinished`. Impede condições de
  corrida em faixas curtas (vinhetas) onde ambos os mecanismos
  avançavam independentemente.
- **Watchdog de silêncio / falha de áudio**: monitora o VU meter via
  `updateDisplay()` (timer de 10ms). Se um player está em `PlayingState`
  mas nenhum áudio chega ao VU meter por 10 segundos (sem fade ativo),
  chama `skipToNext()` automaticamente. Cobre falha de dispositivo,
  bugs silenciosos do decoder e pipes travados.
- **Fix de segfault ao editar playlist durante reprodução**:
  `clearPlaylist()` esvaziava a playlist com `isPlaying` ainda true e
  `current_play` desatualizado, fazendo `topLevelItem()` retornar
  nullptr → SIGSEGV. Agora: `clearPlaylist()` reseta os dois players e
  o estado de reprodução; `updateAudioList()` verifica null de
  `curItem`/`nextItem` antes de estilizar;
  `on_btn_remove_item_clicked()` ajusta `current_play`/`next_play`
  após apagar; `next()` limita `current_play` antes de indexar.
- **Botão Stop funciona com playlist vazia**: remover a última faixa
  (em reprodução) deixava a playlist vazia com áudio ainda tocando —
  `on_btn_stop_clicked()` retornava cedo e nunca parava os players.
  Agora só retorna cedo se a playlist estiver vazia **e** nenhum
  player tocando. Também protegido o acesso `playlist[current_play]`
  no `updateAudioList()` para o caso vazio (era UB fora dos limites).

#### `buttonhole.h`
- Adicionado `#include <QMediaPlayer>` e `#include <QAudioOutput>`
  explicitamente (eram puxados indiretamente via `audioplayer.h`
  quando ele herdava de `QMediaPlayer`)

### Problemas conhecidos (originais, não introduzidos por nós)
- `TagLib::AudioProperties::length()` deprecated — usar
  `lengthInSeconds()` (3 warnings no build)
- Warning do VDPAU em GPUs Radeon (inofensivo, aceleração de vídeo)
- Warning de parsing do tema GTK com certas versões de
  `gtk-contained-dark.css` (inofensivo)
