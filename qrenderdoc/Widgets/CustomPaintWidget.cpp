/******************************************************************************
 * The MIT License (MIT)
 *
 * Copyright (c) 2015-2026 Baldur Karlsson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 ******************************************************************************/

#include "CustomPaintWidget.h"
#include <math.h>
#include <QEvent>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QVBoxLayout>
#include "Code/Interface/QRDInterface.h"
#include "Code/QRDUtils.h"

#if defined(RENDERDOC_WAYLAND_UI)
#include "RenderDocRhiWidget.h"
#endif

CustomPaintWidgetInternal::CustomPaintWidgetInternal(CustomPaintWidget &parentCustom, bool rendering)
    : m_Custom(parentCustom), m_Rendering(rendering)
{
  setAttribute(Qt::WA_OpaquePaintEvent);
  setMouseTracking(true);
  if(m_Rendering)
    setAttribute(Qt::WA_PaintOnScreen);
}

CustomPaintWidgetInternal::~CustomPaintWidgetInternal()
{
}

CustomPaintWidget::CustomPaintWidget(QWidget *parent) : QWidget(parent)
{
  m_Tag = QFormatStr("custompaint%1").arg((uintptr_t)this);

  setAttribute(Qt::WA_OpaquePaintEvent);

  m_Dark = Formatter::DarkCheckerColor();
  m_Light = Formatter::LightCheckerColor();

  QVBoxLayout *l = new QVBoxLayout(this);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(0);
  setLayout(l);

  RecreateInternalWidget();
}

CustomPaintWidget::~CustomPaintWidget()
{
  if(m_Ctx)
    m_Ctx->RemoveCaptureViewer(this);
}

void CustomPaintWidget::SetContext(ICaptureContext &ctx)
{
  if(m_Ctx)
    m_Ctx->RemoveCaptureViewer(this);

  m_Ctx = &ctx;
  m_Ctx->AddCaptureViewer(this);

  RecreateInternalWidget();
}

void CustomPaintWidget::OnCaptureLoaded()
{
  RecreateInternalWidget();
}

void CustomPaintWidget::OnCaptureClosed()
{
  // forget any output we used to have
  SetOutput(NULL);
}

void CustomPaintWidget::OnSelectedEventChanged(uint32_t eventId)
{
  // nothing, we only care about capture loaded/closed events
}

void CustomPaintWidget::OnEventChanged(uint32_t eventId)
{
  // if we've encountered a fatal error recreate the widget and take over painting
  if(m_Rendering && m_Ctx && !m_Ctx->GetFatalError().OK())
  {
    RecreateInternalWidget();
    update();
  }
}

void CustomPaintWidget::update()
{
#if defined(RENDERDOC_WAYLAND_UI)
  if(m_RhiWidget)
  {
    // Schedule our own paintEvent — it triggers renderInternal(), which
    // AsyncInvokes Display() and (on completion) updates the RhiWidget with
    // the new dmabuf contents. Going through paintEvent lets Qt coalesce
    // multiple update() calls per frame down to one render.
    QWidget::update();
    return;
  }
#endif

  if(m_Internal)
    m_Internal->update();
  QWidget::update();
}

#if defined(RENDERDOC_WAYLAND_UI)
bool CustomPaintWidget::useRhiWidget() const
{
  return QGuiApplication::platformName() == QLatin1String("wayland");
}
#endif

WindowingData CustomPaintWidget::GetWidgetWindowingData()
{
  m_Rendering = true;
  RecreateInternalWidget();

#if defined(RENDERDOC_WAYLAND_UI)
  // On Wayland, use headless output with dmabuf export instead of a
  // wl_surface to avoid creating native child widgets (subsurfaces).
  // Allocate the bb at the widget's *physical* pixel size — the texture
  // viewer's fit/zoom/click math is all in physical pixels (it multiplies
  // logical mouse coords by devicePixelRatioF), so bb dims must match or
  // content over/underflows the bb on fractional-scaled displays.
  if(useRhiWidget())
  {
    const qreal dpr = devicePixelRatioF();
    return CreateHeadlessWindowingData((int)(width() * dpr), (int)(height() * dpr));
  }
#endif

  // switch to rendering here and recreate the widget, so we have an updated winId for the windowing
  // data
  return m_Ctx->CreateWindowingData(m_Internal);
}

void CustomPaintWidget::SetOutput(IReplayOutput *out)
{
  m_Output = out;
  m_Rendering = (out != NULL);

  RecreateInternalWidget();

#if defined(RENDERDOC_WAYLAND_UI)
  // Feed the dmabuf fd to the RhiWidget after the output is created. Each
  // widget samples its own sub-output's dmabuf (main vs pixel context) — the
  // two outputs live inside a single ReplayOutput but have distinct bbs.
  if(m_RhiWidget && m_Output)
  {
    int fd = m_PixelContextMode ? m_Output->GetPixelContextDmabufFd() : m_Output->GetDmabufFd();
    int stride = m_PixelContextMode ? m_Output->GetPixelContextDmabufStride()
                                    : m_Output->GetDmabufStride();
    auto dims = m_PixelContextMode ? m_Output->GetPixelContextDimensions() : m_Output->GetDimensions();
    if(fd >= 0)
      m_RhiWidget->setDmabuf(fd, (uint32_t)dims.first, (uint32_t)dims.second, (uint32_t)stride);
  }
#endif
}

void CustomPaintWidget::RecreateInternalWidget()
{
  if(!GUIInvoke::onUIThread())
  {
    GUIInvoke::call(this, [this]() { RecreateInternalWidget(); });
    return;
  }

  // if no capture is loaded, or we've encountered a fatal error, we're not rendering anymore.
  m_Rendering = m_Rendering && m_Ctx && m_Ctx->IsCaptureLoaded() && m_Ctx->GetFatalError().OK();

#if defined(RENDERDOC_WAYLAND_UI)
  if(useRhiWidget())
  {
    // On Wayland, use QRhiWidget for rendering to avoid wl_subsurfaces.
    // Non-rendering state uses the regular internal widget for checkerboard.
    if(m_Rendering)
    {
      if(!m_RhiWidget)
      {
        delete m_Internal;
        m_Internal = NULL;

        m_RhiWidget = new RenderDocRhiWidget(this);
        m_RhiWidget->setMouseTracking(true);
        layout()->addWidget(m_RhiWidget);

        // Forward focus from this parent to the RhiWidget so callers like
        // BufferViewer's ui->render->setFocus() actually focus the child that
        // receives key events (mesh viewer flycam relies on this).
        setFocusProxy(m_RhiWidget);
      }
    }
    else
    {
      if(m_RhiWidget || m_Internal == NULL)
      {
        setFocusProxy(nullptr);

        delete m_RhiWidget;
        m_RhiWidget = NULL;

        delete m_Internal;
        m_Internal = new CustomPaintWidgetInternal(*this, false);
        layout()->addWidget(m_Internal);
      }
    }
    return;
  }
#endif

  // we need to recreate the widget if it's not matching out rendering state.
  if(m_Internal == NULL || m_Rendering != m_Internal->IsRendering())
  {
    delete m_Internal;
    m_Internal = new CustomPaintWidgetInternal(*this, m_Rendering);

    layout()->addWidget(m_Internal);
  }
}

void CustomPaintWidget::resizeEvent(QResizeEvent *e)
{
#if defined(RENDERDOC_WAYLAND_UI)
  // For the RhiWidget path, push dimensions from the parent widget's
  // resize event since there's no CustomPaintWidgetInternal to do it. Route
  // to main vs pixel-context sub-output depending on this widget's role,
  // and pass *physical* pixels (logical * dpr) to match the texture
  // viewer's coordinate space — see GetWidgetWindowingData.
  if(m_RhiWidget && m_Output)
  {
    const qreal dpr = devicePixelRatioF();
    const int w = (int)(e->size().width() * dpr);
    const int h = (int)(e->size().height() * dpr);
    if(m_PixelContextMode)
      m_Output->SetPixelContextDimensions(w, h);
    else
      m_Output->SetDimensions(w, h);
  }
#endif

  QWidget::resizeEvent(e);
}

void CustomPaintWidget::changeEvent(QEvent *event)
{
  if(event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
  {
    m_Dark = Formatter::DarkCheckerColor();
    m_Light = Formatter::LightCheckerColor();
    update();
  }
}

void CustomPaintWidget::renderInternal(QPaintEvent *e)
{
  if(m_Ctx && m_Output && m_Ctx->IsCaptureLoaded())
  {
    QPointer<CustomPaintWidget> me(this);
    m_Ctx->Replay().AsyncInvoke(m_Tag, [me](IReplayController *r) {
      if(me && me->m_Output && me->m_Ctx->IsCaptureLoaded())
      {
        me->m_Output->Display();

#if defined(RENDERDOC_WAYLAND_UI)
        // After rendering completes, tell the RhiWidget to repaint with the
        // new dmabuf contents — pulling from the right sub-output.
        if(me->m_RhiWidget)
        {
          const bool pixelContext = me->m_PixelContextMode;
          int fd = pixelContext ? me->m_Output->GetPixelContextDmabufFd()
                                : me->m_Output->GetDmabufFd();
          int stride = pixelContext ? me->m_Output->GetPixelContextDmabufStride()
                                    : me->m_Output->GetDmabufStride();
          auto dims = pixelContext ? me->m_Output->GetPixelContextDimensions()
                                   : me->m_Output->GetDimensions();
          GUIInvoke::call(me, [me, fd, stride, dims]() {
            if(me && me->m_RhiWidget && fd >= 0)
              me->m_RhiWidget->setDmabuf(fd, (uint32_t)dims.first, (uint32_t)dims.second,
                                          (uint32_t)stride);
          });
        }
#endif
      }
    });
  }
}

void CustomPaintWidget::paintInternal(QPaintEvent *e)
{
  if(m_BackCol.isValid())
  {
    QPainter p(m_Internal);
    p.fillRect(rect(), m_BackCol);
  }
  else
  {
    int numX = (int)ceil((float)rect().width() / 64.0f);
    int numY = (int)ceil((float)rect().height() / 64.0f);

    QPainter p(m_Internal);
    for(int x = 0; x < numX; x++)
    {
      for(int y = 0; y < numY; y++)
      {
        QColor &col = ((x % 2) == (y % 2)) ? m_Dark : m_Light;

        p.fillRect(QRect(x * 64, y * 64, 64, 64), col);
      }
    }
  }
}

void CustomPaintWidget::resizeInternal(QResizeEvent *e)
{
  // Push the new surface dimensions to the replay output so that windowing
  // systems without on-demand size queries (e.g. Wayland) can detect resizes.
  if(m_Output)
    m_Output->SetDimensions(e->size().width(), e->size().height());
}

void CustomPaintWidgetInternal::mousePressEvent(QMouseEvent *e)
{
  emit m_Custom.clicked(e);
}

void CustomPaintWidgetInternal::mouseReleaseEvent(QMouseEvent *e)
{
  emit m_Custom.unclicked(e);
}

void CustomPaintWidgetInternal::mouseDoubleClickEvent(QMouseEvent *event)
{
  emit m_Custom.doubleClicked(event);
}

void CustomPaintWidgetInternal::mouseMoveEvent(QMouseEvent *e)
{
  emit m_Custom.mouseMove(e);
}

void CustomPaintWidgetInternal::wheelEvent(QWheelEvent *e)
{
  emit m_Custom.mouseWheel(e);
}

void CustomPaintWidgetInternal::resizeEvent(QResizeEvent *e)
{
  m_Custom.resizeInternal(e);
  emit m_Custom.resize(e);
}

void CustomPaintWidget::keyPressEvent(QKeyEvent *e)
{
  emit keyPress(e);
}

void CustomPaintWidget::keyReleaseEvent(QKeyEvent *e)
{
  emit keyRelease(e);
}

void CustomPaintWidget::paintEvent(QPaintEvent *e)
{
#if defined(RENDERDOC_WAYLAND_UI)
  // On Wayland the replay output is a dmabuf-exported VkImage that the
  // RhiWidget composites. paintEvent here drives the replay-thread Display()
  // — paint events are naturally coalesced by Qt, so this throttles render
  // work to ~one per frame regardless of how many update() calls happen.
  if(m_RhiWidget)
  {
    renderInternal(e);
    return;
  }
#endif
  // don't paint this widget
}

void CustomPaintWidgetInternal::paintEvent(QPaintEvent *e)
{
  if(m_Rendering)
    m_Custom.renderInternal(e);
  else
    m_Custom.paintInternal(e);
}

#if defined(RENDERDOC_PLATFORM_APPLE)
bool CustomPaintWidgetInternal::event(QEvent *e)
{
  if(m_Rendering && e->type() == QEvent::UpdateRequest)
    paintEvent(NULL);
  return QWidget::event(e);
}
#endif
