package ui

import (
	"image/color"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/theme"
)

// Catppuccin Mocha palette
var (
	colRosewater = color.NRGBA{0xf5, 0xe0, 0xdc, 0xff}
	colMauve     = color.NRGBA{0xcb, 0xa6, 0xf7, 0xff}
	colRed       = color.NRGBA{0xf3, 0x8b, 0xa8, 0xff}
	colPeach     = color.NRGBA{0xfa, 0xb3, 0x87, 0xff}
	colYellow    = color.NRGBA{0xf9, 0xe2, 0xaf, 0xff}
	colGreen     = color.NRGBA{0xa6, 0xe3, 0xa1, 0xff}
	colTeal      = color.NRGBA{0x94, 0xe2, 0xd5, 0xff}
	colBlue      = color.NRGBA{0x89, 0xb4, 0xfa, 0xff}
	colLavender  = color.NRGBA{0xb4, 0xbe, 0xfe, 0xff}

	colText     = color.NRGBA{0xcd, 0xd6, 0xf4, 0xff}
	colSubtext1 = color.NRGBA{0xba, 0xc2, 0xde, 0xff}
	colSubtext0 = color.NRGBA{0xa6, 0xad, 0xc8, 0xff}
	colOverlay1 = color.NRGBA{0x7f, 0x84, 0x9c, 0xff}
	colOverlay0 = color.NRGBA{0x6c, 0x70, 0x86, 0xff}
	colSurface2 = color.NRGBA{0x58, 0x5b, 0x70, 0xff}
	colSurface1 = color.NRGBA{0x45, 0x47, 0x5a, 0xff}
	colSurface0 = color.NRGBA{0x31, 0x32, 0x44, 0xff}
	colBase     = color.NRGBA{0x1e, 0x1e, 0x2e, 0xff}
	colMantle   = color.NRGBA{0x18, 0x18, 0x25, 0xff}
	colCrust    = color.NRGBA{0x11, 0x11, 0x1b, 0xff}
)

// Semantic aliases used by the view (replace your old definitions of these).
var (
	colWork    = colLavender
	colBreak   = colGreen
	colPaused  = colOverlay1
	colSubtext = colSubtext0
)

type mochaTheme struct{ fyne.Theme }

// NewMochaTheme returns a Catppuccin Mocha theme with a lavender accent.
func NewMochaTheme() fyne.Theme { return &mochaTheme{theme.DefaultTheme()} }

func (m *mochaTheme) Color(n fyne.ThemeColorName, _ fyne.ThemeVariant) color.Color {
	switch n {
	case theme.ColorNameBackground:
		return colBase
	case theme.ColorNameMenuBackground, theme.ColorNameOverlayBackground:
		return colMantle
	case theme.ColorNameForeground:
		return colText
	case theme.ColorNamePlaceHolder, theme.ColorNameDisabled:
		return colOverlay1
	case theme.ColorNamePrimary, theme.ColorNameFocus, theme.ColorNameHyperlink:
		return colLavender
	case theme.ColorNameForegroundOnPrimary:
		return colCrust
	case theme.ColorNameButton, theme.ColorNameInputBackground:
		return colSurface0
	case theme.ColorNameDisabledButton:
		return colMantle
	case theme.ColorNameHover:
		return color.NRGBA{0xb4, 0xbe, 0xfe, 0x22}
	case theme.ColorNamePressed:
		return color.NRGBA{0xb4, 0xbe, 0xfe, 0x40}
	case theme.ColorNameSelection:
		return color.NRGBA{0xb4, 0xbe, 0xfe, 0x40}
	case theme.ColorNameSeparator, theme.ColorNameInputBorder:
		return colSurface1
	case theme.ColorNameScrollBar:
		return colSurface2
	case theme.ColorNameShadow:
		return color.NRGBA{0x00, 0x00, 0x00, 0x66}
	case theme.ColorNameError:
		return colRed
	case theme.ColorNameSuccess:
		return colGreen
	case theme.ColorNameWarning:
		return colPeach
	}
	return m.Theme.Color(n, theme.VariantDark)
}

func (m *mochaTheme) Size(n fyne.ThemeSizeName) float32 {
	switch n {
	case theme.SizeNamePadding:
		return 6
	case theme.SizeNameInnerPadding:
		return 10
	case theme.SizeNameInputRadius, theme.SizeNameSelectionRadius:
		return 10
	}
	return m.Theme.Size(n)
}
