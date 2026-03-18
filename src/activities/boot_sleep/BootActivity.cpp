#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  // Custom Mod Title
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "Crosspoint: Enhanced Reading Mod", true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_BOOTING));
  // Custom Version Number
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, "ztrawhcs version 2.5.0");
  // Use FULL_REFRESH on first boot: the HALF_REFRESH waveform (used when screen power is off)
  // fakes a high temperature (0x5A) to shorten pulse timing. At actual room temperature this
  // under-drives the E Ink particles, leaving residual charge that appears as a ~5% gray tint
  // before the screen settles. A full-waveform refresh drives every pixel cleanly to white,
  // eliminating the gray flash. The extra ~600ms on boot is not user-visible.
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}
