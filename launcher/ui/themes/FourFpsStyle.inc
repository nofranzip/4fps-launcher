R"QSS(/* 4fps — tema negro y gris con botones blancos para Prism Launcher */

QWidget {
    font-family: "Inter", "Segoe UI", sans-serif;
    font-size: 10pt;
}

QMainWindow, QDialog {
    background-color: #0d0d0e;
}

/* Barras de herramientas (barra superior e inferior de Prism) */
QToolBar {
    background-color: #141416;
    border: none;
    border-bottom: 1px solid #2c2c31;
    padding: 4px;
    spacing: 4px;
}
QToolButton {
    background-color: transparent;
    color: #a1a1aa;
    border: 1px solid transparent;
    border-radius: 6px;
    padding: 6px 10px;
}
QToolButton:hover {
    background-color: #232327;
    color: #ffffff;
}
QToolButton:checked, QToolButton:pressed {
    background-color: #ffffff;
    color: #0d0d0e;
}

/* Botones: blancos con texto negro */
QPushButton {
    background-color: #ffffff;
    color: #0d0d0e;
    border: 1px solid #ffffff;
    border-radius: 6px;
    padding: 7px 16px;
    font-weight: 600;
}
QPushButton:hover {
    background-color: #e4e4e7;
    border-color: #e4e4e7;
}
QPushButton:pressed {
    background-color: #d4d4d8;
}
QPushButton:disabled {
    background-color: #2c2c31;
    border-color: #2c2c31;
    color: #71717a;
}

/* Campos de texto y listas desplegables */
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background-color: #0d0d0e;
    color: #f4f4f5;
    border: 1px solid #2c2c31;
    border-radius: 6px;
    padding: 6px 8px;
    selection-background-color: #ffffff;
    selection-color: #0d0d0e;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
    border-color: #a1a1aa;
}
QComboBox QAbstractItemView {
    background-color: #1b1b1e;
    color: #f4f4f5;
    border: 1px solid #2c2c31;
    selection-background-color: #ffffff;
    selection-color: #0d0d0e;
}

/* Listas, árboles e instancias */
QListView, QTreeView, QTableView {
    background-color: #0d0d0e;
    alternate-background-color: #141416;
    color: #f4f4f5;
    border: 1px solid #2c2c31;
    border-radius: 6px;
}
QListView::item, QTreeView::item {
    border-radius: 6px;
    padding: 4px;
}
QListView::item:hover, QTreeView::item:hover {
    background-color: #232327;
}
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected {
    background-color: #ffffff;
    color: #0d0d0e;
}
QHeaderView::section {
    background-color: #141416;
    color: #a1a1aa;
    border: none;
    border-bottom: 1px solid #2c2c31;
    padding: 6px;
}

/* Pestañas */
QTabWidget::pane {
    border: 1px solid #2c2c31;
    border-radius: 6px;
    background-color: #141416;
}
QTabBar::tab {
    background-color: transparent;
    color: #a1a1aa;
    padding: 8px 14px;
    border-bottom: 2px solid transparent;
}
QTabBar::tab:selected {
    color: #ffffff;
    border-bottom: 2px solid #ffffff;
}
QTabBar::tab:hover {
    color: #ffffff;
}

/* Menús */
QMenuBar, QMenu {
    background-color: #141416;
    color: #f4f4f5;
    border: 1px solid #2c2c31;
}
QMenu::item {
    padding: 6px 20px;
}
QMenu::item:selected, QMenuBar::item:selecte)QSS"
R"QSS(d {
    background-color: #ffffff;
    color: #0d0d0e;
}

/* Grupos, casillas y barras */
QGroupBox {
    border: 1px solid #2c2c31;
    border-radius: 8px;
    margin-top: 14px;
    padding-top: 8px;
    color: #a1a1aa;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
}
QCheckBox::indicator, QRadioButton::indicator {
    width: 14px;
    height: 14px;
    border: 1px solid #52525b;
    background-color: #0d0d0e;
}
QCheckBox::indicator { border-radius: 3px; }
QRadioButton::indicator { border-radius: 7px; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked {
    background-color: #ffffff;
    border-color: #ffffff;
}
QProgressBar {
    background-color: #0d0d0e;
    border: 1px solid #2c2c31;
    border-radius: 4px;
    text-align: center;
    color: #f4f4f5;
}
QProgressBar::chunk {
    background-color: #ffffff;
    border-radius: 3px;
}
QSlider::groove:horizontal {
    height: 6px;
    background: #0d0d0e;
    border: 1px solid #2c2c31;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    width: 16px;
    margin: -6px 0;
    background: #ffffff;
    border-radius: 8px;
}
QSlider::sub-page:horizontal {
    background: #ffffff;
    border-radius: 3px;
}

/* Barras de desplazamiento */
QScrollBar:vertical, QScrollBar:horizontal {
    background: #0d0d0e;
    border: none;
    width: 10px;
    height: 10px;
}
QScrollBar::handle:vertical, QScrollBar::handle:horizontal {
    background: #3a3a40;
    border-radius: 5px;
    min-height: 24px;
    min-width: 24px;
}
QScrollBar::handle:hover {
    background: #71717a;
}
QScrollBar::add-line, QScrollBar::sub-line {
    width: 0;
    height: 0;
}

QToolTip {
    background-color: #232327;
    color: #f4f4f5;
    border: 1px solid #3a3a40;
    padding: 4px 8px;
}
QStatusBar {
    background-color: #141416;
    color: #a1a1aa;
    border-top: 1px solid #2c2c31;
}

/* ---- 4fps: interfaz del disenio ---- */
QWidget#fpsRoot { background-color: #0d0d0e; }
QFrame#fpsSide { background-color: #141416; border-right: 1px solid #2c2c31; }
QPushButton#navButton { background-color: transparent; color: #a1a1aa; border: none; text-align: left; padding: 11px 14px; border-radius: 6px; font-size: 10.5pt; font-weight: 500; }
QPushButton#navButton:hover { background-color: #232327; color: #ffffff; }
QPushButton#navButton:checked { background-color: #ffffff; color: #0d0d0e; font-weight: 600; }
QStackedWidget#fpsStack, QWidget#fpsPage, QWidget#fpsGridHost, QScrollArea#fpsScroll { background-color: #0d0d0e; border: none; }
QLabel#fpsTitle { color: #ffffff; font-size: 20pt; font-weight: 700; }
QLabel#fpsSubtitle { color: #a1a1aa; font-size: 10.5pt; }
QLabel#fpsMuted { color: #a1a1aa; font-size: 9pt; }
QLabel#fpsHint { color: #71717a; font-size: 8.5pt; }
QLabel#fpsCaption { color: #a1a1aa; font-size: 8pt; font-weight: 700; letter-spacing: 1px; }
QLabel#fpsSectionTitle { color: #ffffff; font-size: 12pt; font-weight: 600; }
QLabel#fpsRowTitle, QLabel#fpsCardTitle { color: #ffff)QSS"
R"QSS(ff; font-size: 10.5pt; font-weight: 600; }
QLabel#fpsDetailTitle { color: #ffffff; font-size: 14pt; font-weight: 700; }
QLabel#fpsStatValue { color: #ffffff; font-weight: 700; }
QLabel#fpsBadge { background-color: #ffffff; color: #0d0d0e; border-radius: 10px; padding: 2px 9px; font-size: 8pt; font-weight: 700; }
QLabel#fpsActivePill { background-color: #ffffff; color: #0d0d0e; border-radius: 12px; padding: 4px 14px; font-weight: 700; }
QLabel#fpsAvatar { background-color: #2c2c31; border-radius: 4px; }
QFrame#fpsPanel { background-color: #141416; border: 1px solid #2c2c31; border-radius: 10px; }
QFrame#fpsStat { background-color: #0d0d0e; border: 1px solid #2c2c31; border-radius: 6px; }
QFrame#fpsSeparator { background-color: #2c2c31; border: none; }
QFrame#fpsCard { background-color: #141416; border: 1px solid #2c2c31; border-radius: 10px; }
QFrame#fpsCard:hover { border-color: #52525b; }
QFrame#fpsCard[selected="true"] { border: 2px solid #ffffff; }
QPushButton#fpsAddCard { background-color: transparent; color: #a1a1aa; border: 1px dashed #3a3a40; border-radius: 10px; font-size: 10.5pt; }
QPushButton#fpsAddCard:hover { color: #ffffff; border-color: #71717a; }
QPushButton#fpsPrimary, QPushButton#fpsCardPlay { background-color: #ffffff; color: #0d0d0e; border: 1px solid #ffffff; border-radius: 6px; padding: 8px 16px; font-weight: 600; }
QPushButton#fpsPrimary:hover, QPushButton#fpsCardPlay:hover { background-color: #e4e4e7; }
QPushButton#fpsCardPlay { padding: 6px 10px; }
QPushButton#fpsOutline, QPushButton#fpsRowButton { background-color: transparent; color: #f4f4f5; border: 1px solid #3a3a40; border-radius: 6px; padding: 8px 16px; font-weight: 500; }
QPushButton#fpsRowButton { text-align: left; padding: 8px 12px; }
QPushButton#fpsOutline:hover, QPushButton#fpsRowButton:hover { background-color: #232327; border-color: #52525b; }
QPushButton#fpsIconButton { background-color: transparent; color: #f4f4f5; border: 1px solid #3a3a40; border-radius: 6px; padding: 0; font-size: 14pt; font-weight: 700; }
QPushButton#fpsIconButtonFlat { background-color: transparent; border: none; padding: 0; }
QPushButton#fpsIconButton:hover, QPushButton#fpsIconButtonFlat:hover { background-color: #232327; }
QPushButton#fpsChip { background-color: transparent; color: #a1a1aa; border: 1px solid #2c2c31; border-radius: 14px; padding: 6px 14px; font-weight: 500; }
QPushButton#fpsChip:hover { color: #ffffff; }
QPushButton#fpsChip:checked { background-color: #ffffff; color: #0d0d0e; border-color: #ffffff; font-weight: 700; }
QLineEdit#fpsSearch, QLineEdit#fpsInput { background-color: #0d0d0e; border: 1px solid #2c2c31; border-radius: 6px; padding: 9px 12px; font-size: 10.5pt; }
QFrame#fpsBottom { background-color: #141416; border-top: 1px solid #2c2c31; }
QLabel#fpsBottomName { color: #f4f4f5; }
QComboBox#fpsCombo { background-color: #141416; border: 1px solid #3a3a40; border-radius: 6px; padding: 6px 14px; font-weight: 600; }
QPushButton#playButton { background-color: #ffff)QSS"
R"QSS(ff; color: #0d0d0e; border: none; border-radius: 6px; font-size: 13pt; font-weight: 800; padding: 10px 28px; }
QPushButton#playButton:hover { background-color: #e4e4e7; }
QPushButton#playButton:pressed { background-color: #d4d4d8; }
QPushButton#playButton:disabled { background-color: #2c2c31; color: #71717a; }
QFrame#fpsTile { background-color: #232327; border: 1px solid #2c2c31; border-radius: 8px; }
QPushButton#fpsSegment { background-color: #0d0d0e; color: #f4f4f5; border: 1px solid #2c2c31; border-radius: 6px; padding: 9px 8px; font-weight: 600; }
QPushButton#fpsSegment:hover { border-color: #52525b; }
QPushButton#fpsSegment:checked { background-color: #ffffff; color: #0d0d0e; border-color: #ffffff; }
QFrame#fpsTabBar { border-bottom: 1px solid #2c2c31; }
QPushButton#fpsTab { background-color: transparent; color: #a1a1aa; border: none; border-bottom: 2px solid transparent; border-radius: 0; padding: 8px 14px; font-size: 10.5pt; font-weight: 500; }
QPushButton#fpsTab:hover { color: #ffffff; }
QPushButton#fpsTab:checked { color: #ffffff; border-bottom: 2px solid #ffffff; font-weight: 600; }
QPushButton#fpsFileTile { background-color: #232327; border: 1px solid #2c2c31; border-radius: 8px; padding: 0; }
QPushButton#fpsFileTile:hover { border-color: #52525b; }
QPushButton#fpsFileTile:checked { border: 2px solid #ffffff; }
QPushButton#fpsPoseTile { background-color: #141416; color: #f4f4f5; border: 1px solid #2c2c31; border-radius: 8px; padding: 6px; font-size: 9pt; }
QPushButton#fpsPoseTile:hover { border-color: #52525b; }
QPushButton#fpsPoseTile:checked { border: 2px solid #ffffff; }
QComboBox#fpsSelect { background-color: #0d0d0e; border: 1px solid #2c2c31; border-radius: 6px; padding: 9px 12px; }
QTreeWidget#fpsTable { background-color: transparent; border: none; font-size: 10.5pt; }
QTreeWidget#fpsTable::item { padding: 10px 6px; border: none; }
QTreeWidget#fpsTable::item:selected { background-color: #232327; color: #ffffff; }
QTreeWidget#fpsTable QHeaderView::section { background-color: transparent; color: #a1a1aa; font-size: 8pt; font-weight: 700; border: none; padding: 6px; }
QLabel#fpsLabel { color: #e4e4e7; font-size: 9.5pt; }
QDialog#fpsSkinPanel { background-color: #141416; border: 1px solid #2c2c31; border-radius: 10px; }
QLabel#fpsAxis { color: #ffffff; font-weight: 700; }
QCheckBox#fpsToggle { color: #ffffff; font-weight: 600; padding: 4px 0; }
QToolButton#fpsWardrobeTile { background-color: #141416; color: #e4e4e7; border: 1px solid #2c2c31; border-radius: 8px; padding: 4px; font-size: 8.5pt; text-align: center; }
QToolButton#fpsWardrobeTile:hover { border-color: #52525b; }
QToolButton#fpsWardrobeTile:checked { border: 2px solid #ffffff; background-color: #232327; }
)QSS"
