# Aceleração de vídeo — Guia de configuração e diagnóstico

LaraRadio reproduz vídeo (faixas "music" com vídeo) junto ao áudio. Este guia cobre a
configuração de **decodificação por hardware**, a conversão dos quadros **na GPU** e o
diagnóstico de quedas de quadros (frame drops), com foco especial em máquinas fracas
(ex.: CPU Bobcat E-300 / GPU Radeon HD 6310, o "PALM").

---

## 1. Como o LaraRadio decodifica e desenha vídeo

1. **Decodificação** — `QMediaPlayer` (backend FFmpeg do Qt 6). O Qt escolhe o backend
   de hardware sozinho **ou** você força o backend desejado (ver §3).
2. **Conversão do quadro** — os quadros chegam tipicamente em `NV12` (hardware) ou
   `YUV420P` (software). Desde a versão 1.1.0 esses formatos são enviados **diretamente
   como texturas de plano para a GPU**, onde a conversão YUV→RGB acontece num pequeno
   "pré-passe" próprio — sem passar pelo `toImage()` (que custava CPU por quadro).
   Qualquer outro formato (ex.: `P010` 10 bits) cai automaticamente no caminho antigo,
   igualmente funcional.
3. **Exibição com crossfade** — durante a transição as duas pistas (deck A e deck B)
   são decodificadas e desenhadas ao mesmo tempo; o custo dobra num único momento.

> **Em CPUs Bobcat**, o alvo honesto é **H.264 8 bits ≤ 720p** (ex.: 1280×720, duas
> pistas em crossfade). 1080p dual-deck pode continuar marginal mesmo otimizado —
> não é limitação do LaraRadio, é o teto da máquina.

---

## 2. O que você precisa instalar por fabricante

| Fabricante | Pacotes / driver | Obs. |
|---|---|---|
| **AMD** | mesa + `libva` (Arch: `libva-mesa-driver`; Debian/Ubuntu: `mesa-va-drivers`) | cobre radeonsi (GCN+) e r600 (TeraScale/PALM). UVD 3.0/PALM: só H.264 Main/High, MPEG-2 e VC-1 — **sem** HEVC/VP9/AV1. |
| **Intel** | `intel-media-driver` (iHD, gerações novas) ou `libva-intel-driver` (i965, antigas) | VA-API nativo. |
| **NVIDIA** | Driver proprietário + device type `cuda` (NVDEC), **ou** `nvidia-vaapi-driver` para expor VA-API | Não há VA-API nativo. Use `CUDA` no LaraRadio (ou o bridge VA-API). |

Verificação rápida de backend: no diálogo de configuração (aba **Vídeo**) existe o botão
**"Verificar VA-API (vainfo)"** que roda `vainfo` e mostra o resultado. Em máquinas com
NVIDIA é normal que o `vainfo` não mostre nada útil — nesse caso use a opção CUDA, não
conclua que "o hardware não suporta vídeo".

---

## 3. Configuração no LaraRadio

Menu **Config → Configurações → aba Vídeo**:

- **Decodificação de vídeo por hardware (aplicado ao reiniciar)**
  - `Automático (recomendado)` — o Qt escolhe (VA-API em AMD/Intel, CUDA em NVIDIA).
  - `VA-API — AMD e Intel` — força VA-API.
  - `CUDA — NVIDIA` — força CUDA/NVDEC.
  - `Desligada (CPU)` — desativa **todo** decode por hardware (ex.: para comparar ou por
    compatibilidade).

A opção é salva em `~/.config/LaraRadio/LaraRadio.conf` (seção `[video]`, chave
`hwdecode`) e **vale para a próxima abertura do aplicativo**.

### Equivalência com variáveis de ambiente

| Valor no LaraRadio | Variável `QT_FFMPEG_DECODING_HW_DEVICE_TYPES` |
|---|---|
| `auto` | (não definida — Qt decide) |
| `vaapi` | `vaapi` |
| `cuda` | `cuda` |
| `off` | `,` (lista vazia → desativa HW **totalmente**) |

> Detalhe importante para diagnóstico manual: a variável **definida como string vazia**
> (`=`) também desativa o HW. Só **não definida** significa "automático". O LaraRadio
> trata isso corretamente.

---

## 4. Logs de decodificação

Ative os logs do backend de mídia:

```sh
QT_LOGGING_RULES="qt.multimedia.ffmpeg*=true" lara
```

Exemplos do que procurar:

- **Decode por hardware engajou** — linha contendo algo como `Creating VAAPI HW
  accelerator` (ou `hwaccel`/`deviceType`). Os `Input #0` logo após reproduzir mostram o
  `pix_fmt` (ex.: `yuv420p`) e o perfil do arquivo.
- **Confirmação do formato entregue** — o format do quadro aparece nas linhas de vídeo;
  em geral **NV12 = hardware**, **YUV420P = software**.
- Diagnóstico mais completo (dump de codec/decoder):

  ```sh
  QT_FFMPEG_DEBUG=1 QT_LOGGING_RULES="*.multimedia.*=true" lara
  ```

> Em GPUs antigas pode haver "profile mismatch" (o perfil declarado não bate com os
> caps reportados pelo driver). Se decidir testar, a chave de validação é
> `QT_FFMPEG_HW_ALLOW_PROFILE_MISMATCH=1` — use só para testes; a saída pode ficar
> incorreta.

---

## 5. Diagnóstico de quedas de quadros (frame drops)

**Passo 1 — o arquivo não é o problema.** Confira o codec/resolução:

```sh
ffprobe -v error -select_streams v:0 \
  -show_entries stream=codec_name,width,height,profile,pix_fmt -of default=noprint_wrappers=1 video.mp4
```

**Passo 2 — decodifica por hardware?** Compare `htop` durante a reprodução com
`auto` vs `Desligada (CPU)`. Se a CPU fica saturada nos dois casos, o gargalo é o
caminho de desenho; se `auto` já alivia demais a CPU, o gargalo é outro (rede/IO).

**Passo 3 — quando caem os quadros?**
- **Sempre** → decode ou desenho não acompanham (reduza resolução/data rate, ou revise
  o backend forçado).
- **Só na transição/crossfade** → comportamento esperado: duas pistas decodificando ao
  mesmo tempo. Em CPU fraca, transições 720p fluem; 1080p "empurra com a barriga".

**Passo 4 — transição fluida na tela.** Com duas faixas (ex.: um clipe vermelho e um
azul) e `transition=crossfade`, a troca deve mostrar o blend: vermelho → roxo → azul.
Um pouco de engasgo em CPU Bobcat é normal; em hardware adequado a troca é suave.

---

## 6. Resumo do comportamento na versão 1.1.0

- Nenhum backend de hardware é "chumbado" no código: **mesmo binário funciona em
  AMD, Intel e NVIDIA** via Auto / VA-API / CUDA / Off.
- NV12 (HW) e YUV420P (sw) são convertidos na GPU (texturas de plano + pré-passe YUV);
  demais formatos usam o caminho antigo automaticamente.
- A troca de resolução entre faixas recria as texturas com segurança (sem corrupção).

Para sugestões ou relato de incompatibilidade do seu driver, abra uma issue com a saída
de `vainfo` e do `QT_LOGGING_RULES="qt.multimedia.ffmpeg*=true"` (seção §4).