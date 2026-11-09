# Zero-copy видеопайплайн (GPU-native)

Цель: декодированный кадр остаётся в видеопамяти и передаётся в рендерер
через dma-buf без промежуточных копий в RAM. CPU не касается пиксельных
данных в горячем пути.

## Пайплайн

```
 demuxer ──► decoder (ffmpeg/VAAPI | gstreamer vaapi/v4l2)
                 │  Acquire (VideoFramePool / GstBufferPool / hw_frames_ctx)
                 ▼
        GpuFrameHandle  ── непрозрачный dma-buf дескриптор ──┐
        (формат, размеры, colorspace/range, pts, orientation) │
                 │  emitSignal("frameReady", VideoFrame)      │
                 ▼  (direct, коалесцинг в последний кадр —    │
                 │   см. ниже «Темп воспроизведения»)         │
        Multimedia::VideoOutput                               │
                 │  VideoTextureCache: импорт + draw           │
                 ▼                                            │
        Gui::Painter::drawTexture(RenderTexture) ─────────────┘
                 │  импорт ОДИН раз на poolKey (поверхность пула),
                 │  Painter не знает о видео — рисует произвольную
                 │  текстуру (external-пайплайн по isExternal())
                 ▼
        Firmir::Render device
          ├─ Vulkan: VkImage external memory (VK_EXT_external_memory_dma_buf,
          │   VK_KHR_external_memory_fd, VK_EXT_image_drm_format_modifier),
          │   YCbCr-конверсия в сэмплере (VK_KHR_sampler_ycbcr_conversion)
          └─ OpenGL: EGLImage (EGL_EXT_image_dma_buf_import[(_modifiers)],
              GL_TEXTURE_EXTERNAL_OES, color hints EGL_YUV_COLOR_SPACE_HINT_EXT)
                 │
                 ▼
        swapchain / display
```

## Темп воспроизведения (позиция == реальное время)

Если показ кадров медленнее потока (тяжёлый 4K-ролик на CPU-конверсии),
наивная очередь кадров растёт, и видео «растягивается» (1 секунда за 3).
Этому противостоит цепочка:

1. **appsink (gstreamer)**: `sync=true` (кадры по часам пайплайна),
   `max-buffers=1` + `drop=true` (в очереди только новейший кадр) и
   `qos=true` — события об опоздании уходят вверх по пайплайну, и декодер
   сам начинает пропускать кадры вместо того, чтобы выдавать все с
   опозданием.
2. **Ленивые пиксели**: на CPU-пути бэкенд НЕ копирует кадр при эмиссии —
   VideoFrame держит нативный буфер и читает пиксели через `readback`
   только при фактическом показе (`ensureCpuPixels`).
3. **Коалесцинг в VideoOutput**: из потока декодера кадр попадает в
   mutex-защищённый слот и в GUI-поток постится не более одного события на
   кадр; промежуточные кадры заменяются новыми — рендерится всегда
   последний, очередь и память не растут.

Итог: позиция воспроизведения следует реальному времени, избыточные кадры
выбрасываются до копирования пикселей.

Точки синхронизации:

1. **декодер → рендерер**: бэкенд гарантирует, что запись в поверхность
   завершена до публикации хендла — VAAPI: `vaSyncSurface()` перед
   `av_hwframe_map(DRM_PRIME)`; gstreamer: appsink доставляет буфер только
   после завершения записи декодером (implicit sync dma-buf в драйвере).
2. **рендерер → декодер (reuse)**: VideoOutput держит текущий и предыдущий
   кадры (deferred release): поверхность возвращается в пул не раньше, чем
   показан следующий кадр. Вместе с vsync-циклом это исключает запись в
   сэмплируемую поверхность. На драйверах с `VK_KHR_external_fence_fd` /
   `EGL_ANDROID_native_fence` можно заменить на точные fence'и (TODO; в
   llvmpipe этих расширений нет).
3. **пул**: `VideoFramePool::acquire()` не блокирует — при исчерпании
   возвращает nullptr и тикает `droppedFrames` (декодер дропает кадр, а не
   останавливается). ffmpeg-vaapi дополнительно ограничен пулом hw_frames
   (backpressure внутри avcodec).

## Выбор пути (capability на старте)

- `FIRMIR_MM_ZERO_COPY=0` → принудительный CPU-путь везде.
- ffmpeg: `av_hwdevice_ctx_create(VAAPI)` не удался → software decode
  (path=CpuFallback, лог `open:cpu (no vaapi device)`).
- gstreamer: caps `video/x-raw(memory:DMABuf)` не согласовались (нет
  аппаратного декодера) → RGBA caps (CPU-путь), без изменений в вызывающем
  коде.
- Рендерер: `RenderDevice::supportsDmaBufTextures()` = false (soft/null) или
  `importDmaBufTexture()` вернул nullptr (формат/модификатор не поддержан,
  нет расширений, VM без GPU) → `VideoOutput` вызывает `frame.ensureCpuPixels()`
  (readback через бэкенд) и рисует как раньше. Счётчик
  `VideoOutput::cpuFallbackFrames()` + `VideoMetrics::cpuCopies`.

## Матрица «бэкенд × zero-copy»

| Декодер | Handle | Импорт Vulkan | Импорт OpenGL | Синхронизация | Fallback |
|---|---|---|---|---|---|
| ffmpeg+VAAPI | VASurface→DMA-BUF (AVDRMFrameDescriptor) | external memory + ycbcr sampler | EGLImage + TEXTURE_EXTERNAL_OES | vaSyncSurface + deferred release | av_hwframe_transfer_data + swscale (readback functor) |
| gstreamer | GstMemory dmabuf fd | то же | то же | appsink delivery + implicit sync | gst_buffer_map + NV12→RGBA |
| ffmpeg SW | — (нет handle) | — | — | — | всегда: swscale → RGBA |

Матрица поддержки форматов: NV12, P010, P016, ABGR8 (RGBA в памяти).
YCbCr-матрица BT.601/709/2020 и limited/full передаются в сэмплер
(Vulkan: VkSamplerYcbcrConversion; GL: EGL colorspace hints, Mesa).

## Горячий путь

Без аллокаций текстур на кадр (импорт кэширован по poolKey), без memcpy
пикселей, без glReadPixels/Map текстур декодера, без блокирующих локов
(критическая секция пула — только сдвиг указателя слота).

## Метрики

`VideoSource::metrics()` → `VideoMetrics{path, framesDecoded, cpuCopies,
droppedFrames, decodeTimeUs, poolWaitUs, activeSurfaces}`;
`VideoOutput::{zeroCopyFrames,cpuFallbackFrames}()`. Выбранный путь и бэкенд
логируются при open() (категории `multimedia`, `multimedia.ffmpeg`,
`multimedia.gstreamer`, `render.opengl`).

## Известные ограничения

- **VAAPI-сборка** ffmpeg-бэкенда компилируется только с libva/libva-drm
  (`FIRMIR_MM_HAVE_VAAPI`); в этой сборочной среде libva нет — код написан,
  но runtime-проверка zero-copy пути требует машины с GPU-драйвером.
- llvmpipe (VM): расширения импорта есть (`supportsDmaBufTextures()==true`),
  но нет `/dev/dri` и dma-heap — импорт реального кадра неоткуда взять;
  проверены capability-детект и чистый отказ на битых fd.
- Vulkan без `VK_EXT_image_drm_format_modifier` (llvmpipe): импорт идёт по
  legacy-пути (LINEAR, неявный layout) — на реальных драйверах (ANV/RADV)
  используется explicit modifier.
- HDR (P010 + PQ/HLG): тонемаппинг не выполняется (отдельная задача), но
  путь не падает и не уходит в CPU.
- Синхронизация без внешних fence (см. п.2 выше) — заменить на
  external_fence_fd при наличии драйвера.
