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

#### `mainwindow.h` / `mainwindow.cpp`
- **Pré escuta pelos navegadores de arquivos**: clicar com o botão
  direito em uma linha de qualquer um dos navegadores da esquerda
  (`files` — Músicas, `jingle_files` — Jingles) agora oferece
  **Pré Escuta**, abrindo a mesma janela de pré escuta da playlist.
  Arquivos de mídia tocam como são; pastas pré escutam uma faixa
  aleatória de dentro, igual às pastas da playlist. Arquivos que não
  são mídia (`.xml`, `.png`, `.desktop`, …) não mostram menu, já que
  o player não conseguiria abri-los. Os dois navegadores compartilham
  um único handler de menu de contexto.
- **`cuePreview()` dividido em `cuePath()` / `cuePlay()`**: a criação
  da janela, o `loadAndPlay()` e o raise/activate foram para
  `cuePlay(path, displayName)`, compartilhado entre a playlist e os
  navegadores de arquivos. A escolha aleatória de pasta foi para o
  helper `randomMediaInFolder()`, e o novo `mediaDisplayName()` dá ao
  item do navegador o mesmo rótulo `title - artist` usado na playlist.

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

#### `spectrumanalyzer.h` / `spectrumanalyzer.cpp` (novo)
- **Análise de espectro para a saída de vídeo**: uma FFT radix-2
  iterativa sobre um downmix mono com janela de Hann, alimentada por
  `calculateRMS()` — os mesmos buffers que já movem os VU meters,
  então não há um segundo ponto de escuta no caminho do áudio.
  Nenhuma dependência nova.
- **64 barras, em escala logarítmica onde a transformação permite**:
  as bordas das bandas são espaçadas em log a partir de 30 Hz, depois
  convertidas para bins da FFT (`bin = Hz * kFftSize / taxa`) e cada
  barra recebe pelo menos um bin. Sem esse último passo as bandas mais
  baixas cairiam todas no bin 1 (43 Hz de largura em 44,1 kHz),
  leriano idênticas e o pico ficaria na última banda.
- **Ataque instantâneo, queda gradual**: as barras sobem de uma vez
  num transiente e descem suavemente, e um marcador de pico fica
  pendurado alguns segundos acima delas. Os níveis são escalados de
  -72 a -12 dB.
- **Um filtro, um dono**: `analyse()` agora só calcula o medidor (ataque
  instantâneo, queda cronometrada) enquanto `takeFrame()` só aplica o
  filtro de display e escreve a linha. Antes os dois suavizavam *e* os
  dois escreviam `m_row`, em relógios separados — um filtro rodando ~98
  vezes por segundo em vez de uma vez por repaint, mais duas threads
  disputando os mesmos pixels. O filtro agora é uma constante de tempo
  de 20 ms aplicada uma vez por quadro pintado e só na descida, então o
  ataque não é filtrado. O tempo de um transiente até 90% da escala
  total caiu de 64 ms (5 quadros pintados) para 32 ms (3 quadros).
- **Ritmo atrelado à taxa de atualização da display**: a FFT é
  limitada a uma atualização por quadro desenhado em vez de uma por
  callback de áudio. O intervalo vem de `QScreen::refreshRate()`, então
  o gráfico avança junto com a tela em vez de fixo em 30 fps.
- O que sai daqui para o renderizador é apenas uma imagem RGBA
  64x1 (256 bytes: altura da barra em R, pico em G), copiada sob mutex — o
  callback `audioBufferReceived` roda na thread de multimídia e o
  `paintGL` na thread da GUI.

#### `videomixer.h` / `videomixer.cpp` / `shaders/eqbars.frag` (novo)
- **Passagem de visualizador de EQ**: quando o deck que entra não tem
  vídeo ativo (`!isVideoActive()`) e nenhuma transição está em curso,
  a saída mostra o gráfico de barras em vez de um efeito de transição
  rodando sobre preto. Vídeo real não é tocado, e o `fromHeld` — que
  mantém o último quadro do deck de saída na tela — nunca é apagado no
  meio de um crossfade.
- **`renderEq()`**: o fragment shader desenha as barras, os espaços
  entre elas e o degradê verde→amarelo→vermelho a partir da altura. A
  CPU só envia a linha de 256 bytes por quadro, então o visual é
  inteiramente do lado da GPU.
- Uma passagem dedicada em vez de um `.frag` de efeito, para que
  shaders customizados do usuário continuem com a interface de uma
  única sampler.
- Falhas são permanentes, por shader: um fragment ausente ou quebrado
  é reportado uma vez e depois ignorado, nunca tentado a cada quadro.

#### `shaders/eqcircle.frag` (novo) / `videomixer.h` / `videomixer.cpp`
- **Modo de EQ circular (radial)**: a mesma linha de 64 bandas
  disposta ao redor do centro da saída em vez de encostada na borda
  inferior, com um hub vazio, raios crescendo para fora e o marcador
  de pico no próprio raio. Escolhido com `EqMode::Circle`
  (`video/eqvisualizer = circle`).
- O leque de raios é deslocado meio slot, assim um raio cai em cada eixo
  da tela. Sem isso, as emendas caem exatamente em 0/90/180/270° e
  essas quatro direções saem em branco.
- A emenda em 360° é contornada com `mod(floor(slot), uBands)` em vez
  de limitar o valor: o `atan` devolve exatamente 2π na metade negativa
  da linha central, e limitar mapearia isso para uma tira fina de um
  pixel no fim da linha em vez de voltar para a banda 0.
- As posições são corrigidas pela proporção antes de tomar o raio, então
  uma saída 16:9 recebe um círculo e não uma elipse.
- `ensureEqProgram()` religa quando o modo muda (o caminho do fragment
  com que foi compilado fica memorizado), e as falhas são acompanhadas
  por caminho, para que um visualizador quebrado não derrube o outro.
- **Corrigido um ponteiro nulo**: `refreshIncomingDeck()` chamava
  `m_decks[n]->isVideoActive()` sem proteção no ramo de reserva, então
  mostrar um `VideoMixer` antes de `setDeck()` quebrava.

#### `videomixer.h` / `videomixer.cpp` / `spectrumanalyzer.h` / `spectrumanalyzer.cpp`
- **Redesenho na taxa da display**: o timer de repaint do mixer estava
  fixo em 33 ms (~30 fps), o que limitava o visualizador em 30 fps mesmo
  numa tela mais rápida. Agora ele roda em
  `QScreen::refreshRate()` e é um `Qt::PreciseTimer`, então o repaint
  cai na borda do vsync em vez de derivar em relação a ele. A taxa é
  lida de novo quando a janela vai para outro monitor
  (`ScreenChangeInternal`).
- `syncFrameRate()` é o único lugar que decide a taxa alvo e a passa
  para o analisador, então os dois não podem ficar dessincronizados.
- **Corrigido um bug de ritmo**: o `feed()` dizia limitar a FFT a uma
  por quadro desenhado mas nunca descartava nada — ele calculava o
  tempo decorrido e passava para a queda, mesmo assim rodando a FFT em
  *todos* os callbacks de áudio. Com o AAC de um `.mp4` (buffers de
  1024 quadros) isso dá ~43 FFTs/s, independente do que a display
  conseguisse mostrar. O limite agora é real: buffers que chegam dentro
  do intervalo do quadro são descartados antes da FFT.

#### `videowindow.h` / `videowindow.cpp` / `mainwindow.h` / `mainwindow.cpp`
- Liga o analisador ao mixer. A `MainWindow` é dona do analisador e a
  `VideoMixer` apenas usa o ponteiro. `reset()` é chamado ao parar
  para a saída não continuar mostrando o espectro do último quadro
  depois que o transporte para.

#### `configdialog.ui` / `configdialog.cpp`
- **Visualizador de EQ (aba Vídeo)**: uma combo salva em
  `video/eqvisualizer` com *Desligado* (padrão), *Barras* e *Círculo*.
  Opt-in, então as configurações existentes não mudam.

### Corrigido

#### `spectrumanalyzer.cpp`
- **A FFT calculava a parte imaginária do twiddle a partir da parte
  real duas vezes**: `vi = re[..] * ci + re[..] * cr`, onde o segundo
  `re` deveria ser `im`. Cada borboleta corrompia `im` já no primeiro
  estágio, deixando um piso de lixo broadband 20–30 dB abaixo do pico
  em *todas* as bandas. Um tom puro de 1 kHz acendia 31 das 32 barras
  entre 43 e 182/255, então o gráfico parecia uma parede sólida que
  apenas pulsava — acrescentar barras a isso não teria ajudado. O tom
  ainda picava na banda certa, e foi exatamente por isso que a verificação
  de posição do tom passava; agora ela também exige que um tom acenda no
  máximo um quarto das barras. Depois da correção o mesmo tom acende 8
  das 64 e todas as outras bandas ficam em 0 exato.
- **`m_updateTimer` nunca era iniciado**, então `elapsed()` devolvia um
  número negativo gigante e o gate em `feed()` comparava contra um ramo
  `sinceMs < 0` que nunca podia disparar. Foi por isso que a correção de
  ritmo registrada acima não tinha efeito de verdade: cada buffer de áudio
  rodava uma FFT (~38/s vindo de MP3) independente do que a display
  conseguisse mostrar. O timer começa no construtor, o gate volta a
  descartar buffers dentro do intervalo do quadro (200 buffers alimentados
  em sequência agora custam 1 transformação, não 204), e
  `analysisCount()` expõe a contagem para o gate ser verificado
  diretamente em vez de cronometrar um laço.

### Alterado

#### `mainwindow.ui` / `resources.qrc` / `deploy/linux/` / `.github/workflows/appimage.yml`
- **Novo ícone do app**: a janela principal agora define `windowIcon`
  a partir de `:/images/icon.png` (antes não havia ícone algum e caía
  no genérico do Qt). `images/icon.png` (1024×1024) é o arquivo master
  e é embutido pelo `resources.qrc`; o antigo
  `deploy/linux/lararadio.png` foi substituído por
  `deploy/linux/icon.png` (512×512, regere com
  `convert images/icon.png -resize 512x512 -strip deploy/linux/icon.png`).
  São dois tamanhos porque o linuxdeploy **rejeita** qualquer resolução
  fora dos tamanhos hiconf (máx. 512×512) e aborta o build do AppImage,
  enquanto o Qt aceita o master. A chave `Icon=` do `.desktop` agora é
  `icon` — sem extensão, como o appimagetool resolve
  `AppDir/<Icon=>.png`.

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
