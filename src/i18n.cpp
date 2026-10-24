#include "i18n.h"

#include <windows.h>

#include <string_view>
#include <unordered_map>

namespace {

struct Entry {
    const char* tr;
    const char* en;
    const char* de;
    const char* zh;
    const char* ja;
};

// Format specifiers must appear in the same order in every language.
const Entry kEntries[] = {
    // --- Navigation and common
    {"Tuş Atama", "Keymap", "Tastenbelegung", "按键映射", "キー割り当て"},
    {"Performans", "Performance", "Performance", "性能", "パフォーマンス"},
    {"Gelişmiş Tuşlar", "Advanced Keys", "Erweiterte Tasten", "高级按键", "高度なキー"},
    {"Makrolar", "Macros", "Makros", "宏", "マクロ"},
    {"Işık", "Lighting", "Beleuchtung", "灯光", "ライティング"},
    {"Cihaz", "Device", "Gerät", "设备", "デバイス"},
    {"Dil", "Language", "Sprache", "语言", "言語"},
    {"Ayarlar", "Settings", "Einstellungen", "设置", "設定"},
    {"Kaydet", "Save", "Speichern", "保存", "保存"},
    {"Sil", "Delete", "Löschen", "删除", "削除"},
    {"Düzenle", "Edit", "Bearbeiten", "编辑", "編集"},
    {"Tamam", "OK", "OK", "确定", "OK"},
    {"Vazgeç", "Cancel", "Abbrechen", "取消", "キャンセル"},
    {"Yok", "None", "Keine", "无", "なし"},
    {"Kapalı", "Off", "Aus", "关闭", "オフ"},
    {"Normal", "Normal", "Normal", "正常", "通常"},
    {"Ters", "Reverse", "Umgekehrt", "反向", "逆方向"},
    {"Profil %d", "Profile %d", "Profil %d", "配置 %d", "プロファイル %d"},
    {"Yeniden ara", "Search again", "Erneut suchen", "重新搜索", "再検索"},
    {"Yeniden oku", "Reload", "Neu laden", "重新读取", "再読み込み"},
    {"— seç —", "— choose —", "— wählen —", "— 选择 —", "— 選択 —"},
    {"Boş bırak", "Leave empty", "Leer lassen", "留空", "空にする"},

    // --- Connection status
    {"Klavye bulunamadı", "Keyboard not found", "Tastatur nicht gefunden", "未找到键盘", "キーボードが見つかりません"},
    {"Bağlanıyor…", "Connecting…", "Verbinde…", "正在连接…", "接続中…"},
    {"Klavye tanınıyor…", "Identifying keyboard…", "Tastatur wird erkannt…", "正在识别键盘…", "キーボードを認識中…"},
    {"Ayarlar okunuyor…", "Reading settings…", "Einstellungen werden gelesen…", "正在读取设置…", "設定を読み込み中…"},
    {"Bağlı", "Connected", "Verbunden", "已连接", "接続済み"},
    {"Klavye bootloader modunda (firmware güncellemesini siteden yapın)",
     "Keyboard is in bootloader mode (update the firmware on the vendor website)",
     "Tastatur im Bootloader-Modus (Firmware-Update über die Hersteller-Website)",
     "键盘处于 Bootloader 模式（请在官网更新固件）",
     "キーボードはブートローダーモードです（ファームウェア更新はメーカーサイトで）"},
    {"Klavye açılamadı (başka bir program kullanıyor olabilir).",
     "Could not open the keyboard (another program may be using it).",
     "Tastatur konnte nicht geöffnet werden (evtl. von einem anderen Programm belegt).",
     "无法打开键盘（可能被其他程序占用）。",
     "キーボードを開けません（他のプログラムが使用中の可能性があります）。"},
    {"Klavye cevap vermiyor.", "The keyboard is not responding.", "Die Tastatur antwortet nicht.", "键盘无响应。",
     "キーボードが応答しません。"},
    {"Direct3D 11 başlatılamadı.", "Could not initialize Direct3D 11.",
     "Direct3D 11 konnte nicht initialisiert werden.", "无法初始化 Direct3D 11。",
     "Direct3D 11 を初期化できませんでした。"},
    {"Slice75 HE klavyeyi USB kablosuyla bağlayın. Program klavyeyi otomatik olarak bulur.\n\nTarayıcıda Chilkey "
     "sürücü sayfası açıksa kapatın; aynı anda yalnızca bir program klavyeyle konuşmalıdır.",
     "Connect the Slice75 HE with a USB cable. The app finds the keyboard automatically.\n\nClose the vendor's "
     "driver web page in your browser if it is open; only one program should talk to the keyboard at a time.",
     "Schließen Sie die Slice75 HE per USB-Kabel an. Die App findet die Tastatur automatisch.\n\nSchließen Sie die "
     "Treiber-Webseite des Herstellers im Browser, falls geöffnet; es sollte immer nur ein Programm mit der "
     "Tastatur kommunizieren.",
     "请用 USB 线连接 Slice75 HE，程序会自动识别键盘。\n\n如果浏览器中打开了厂商的驱动网页，请将其关闭；"
     "同一时间只能有一个程序与键盘通信。",
     "Slice75 HE を USB ケーブルで接続してください。アプリが自動でキーボードを検出します。\n\nブラウザでメーカーの"
     "ドライバーページを開いている場合は閉じてください。キーボードと通信できるのは同時に 1 つのプログラムだけです。"},

    // --- Keyboard selection toolbar
    {"Tümünü seç", "Select all", "Alle auswählen", "全选", "すべて選択"},
    {"Seçimi temizle", "Clear selection", "Auswahl aufheben", "清除选择", "選択解除"},
    {"Harfler", "Letters", "Buchstaben", "字母", "文字"},
    {"%zu tuş seçili  ·  Ctrl+tık: ekle/çıkar", "%zu keys selected  ·  Ctrl+click: add/remove",
     "%zu Tasten ausgewählt  ·  Strg+Klick: hinzufügen/entfernen", "已选 %zu 个键  ·  Ctrl+单击：添加/移除",
     "%zu キー選択中  ·  Ctrl+クリック：追加/解除"},
    {"→ Eş tuşu klavyede tıklayın", "→ Click the partner key on the keyboard",
     "→ Partnertaste auf der Tastatur anklicken", "→ 在键盘图上点击配对键", "→ キーボードでペアキーをクリック"},

    // --- Keycode categories
    {"Harf", "Letters", "Buchstaben", "字母", "文字"},
    {"Sayı", "Numbers", "Zahlen", "数字", "数字"},
    {"Sembol", "Symbols", "Symbole", "符号", "記号"},
    {"Temel", "Basic", "Basis", "基本", "基本"},
    {"Fonksiyon", "Function", "Funktion", "功能", "機能"},
    {"Kontrol", "Control", "Steuerung", "控制", "コントロール"},
    {"Fare", "Mouse", "Maus", "鼠标", "マウス"},

    // --- Keymap tab
    {"Fn0 · Temel katman", "Fn0 · Base layer", "Fn0 · Basisebene", "Fn0 · 基础层", "Fn0 · ベースレイヤー"},
    {"Seçili tuş", "Selected key", "Ausgewählte Taste", "已选按键", "選択中のキー"},
    {"Seçili tuşlar", "Selected keys", "Ausgewählte Tasten", "已选按键", "選択中のキー"},
    {"Klavye görselinden bir veya daha fazla tuş seçin, sonra soldan yeni görevini tıklayın.",
     "Select one or more keys on the keyboard image, then click the new function on the left.",
     "Wählen Sie eine oder mehrere Tasten im Tastaturbild und klicken Sie links auf die neue Belegung.",
     "在键盘图上选择一个或多个键，然后在左侧点击新功能。",
     "キーボード画像でキーを選び、左側で新しい割り当てをクリックします。"},
    {"Fiziksel tuş: %s", "Physical key: %s", "Physische Taste: %s", "物理按键：%s", "物理キー：%s"},
    {"Katman: Fn%d", "Layer: Fn%d", "Ebene: Fn%d", "层：Fn%d", "レイヤー：Fn%d"},
    {"%zu tuş seçili", "%zu keys selected", "%zu Tasten ausgewählt", "已选 %zu 个键", "%zu キー選択中"},
    {"Gelişmiş: %s", "Advanced: %s", "Erweitert: %s", "高级：%s", "高度：%s"},
    {"Varsayılana döndür", "Restore default", "Standard wiederherstellen", "恢复默认", "初期値に戻す"},
    {"Katmanlar", "Layers", "Ebenen", "层", "レイヤー"},
    {"Fn1–Fn3 katmanları, klavyedeki ilgili Fn tuşu basılıyken geçerli olur. Bir tuşu Fn katmanına atamak için "
     "“Fonksiyon” sekmesindeki Fn1–Fn3'ü kullanın.",
     "Layers Fn1–Fn3 are active while the matching Fn key is held. To make a key switch layers, assign Fn1–Fn3 "
     "from the “Function” category.",
     "Die Ebenen Fn1–Fn3 gelten, solange die entsprechende Fn-Taste gehalten wird. Um eine Taste zur "
     "Ebenen-Taste zu machen, weisen Sie Fn1–Fn3 aus der Kategorie „Funktion“ zu.",
     "按住对应的 Fn 键时 Fn1–Fn3 层生效。要让某个键切换层，请在“功能”分类中分配 Fn1–Fn3。",
     "Fn1〜Fn3 レイヤーは対応する Fn キーを押している間有効です。キーにレイヤー切替を割り当てるには「機能」"
     "カテゴリの Fn1〜Fn3 を使います。"},

    // --- Performance tab
    {"Ayarlamak için klavye görselinden tuş seçin. “Tümünü seç” ile bütün klavyeyi, “WASD” ile oyun tuşlarını "
     "hızlıca seçebilirsiniz.",
     "Select keys on the keyboard image to adjust them. Use “Select all” for the whole keyboard or “WASD” to "
     "quickly pick the gaming keys.",
     "Wählen Sie Tasten im Tastaturbild aus. Mit „Alle auswählen“ die ganze Tastatur, mit „WASD“ schnell die "
     "Spieltasten.",
     "在键盘图上选择要调整的键。“全选”选择整个键盘，“WASD”快速选择游戏键。",
     "キーボード画像で調整するキーを選択します。「すべて選択」でキーボード全体、「WASD」でゲーム用キーをすばやく"
     "選べます。"},
    {"Genel ayar", "Global setting", "Globale Einstellung", "全局设置", "全体設定"},
    {"Tek tuş", "Per key", "Pro Taste", "单键", "キー個別"},
    {"Genel basma noktasını kullanıyor (%.2f mm).", "Uses the global actuation point (%.2f mm).",
     "Verwendet den globalen Auslösepunkt (%.2f mm).", "使用全局触发点（%.2f mm）。",
     "全体アクチュエーションポイントを使用（%.2f mm）。"},
    {"İlk tetikleme noktası", "First actuation point", "Erster Auslösepunkt", "首次触发点",
     "初回アクチュエーションポイント"},
    {"Basma noktası", "Actuation point", "Auslösepunkt", "触发点", "アクチュエーションポイント"},
    {"RT basma hassasiyeti", "RT press sensitivity", "RT-Empfindlichkeit (Drücken)", "RT 按下灵敏度",
     "RT 押下感度"},
    {"RT bırakma hassasiyeti", "RT release sensitivity", "RT-Empfindlichkeit (Loslassen)", "RT 松开灵敏度",
     "RT リリース感度"},
    {"Basma ölü bölgesi", "Press deadzone", "Totzone (Drücken)", "按下死区", "押下デッドゾーン"},
    {"Bırakma ölü bölgesi", "Release deadzone", "Totzone (Loslassen)", "松开死区", "リリースデッドゾーン"},
    {"Switch tipi", "Switch type", "Switch-Typ", "轴体类型", "スイッチの種類"},
    {"Genel", "Global", "Global", "全局", "全体"},
    {"Genel basma noktası", "Global actuation point", "Globaler Auslösepunkt", "全局触发点",
     "全体アクチュエーションポイント"},
    {"Gecikmeyi mümkün olduğunca düşürür; Rapid Trigger hassasiyeti biraz azalır.",
     "Minimizes latency; Rapid Trigger precision is slightly reduced.",
     "Minimiert die Latenz; die Rapid-Trigger-Genauigkeit sinkt leicht.",
     "尽可能降低延迟；Rapid Trigger 精度会略微降低。", "遅延を最小化します。Rapid Trigger の精度はやや下がります。"},
    {"Üst ölü bölge", "Top deadzone", "Obere Totzone", "顶部死区", "上部デッドゾーン"},
    {"Tuş derinliği testi", "Key travel test", "Tastenweg-Test", "键程测试", "キーストロークテスト"},
    {"Testi başlat", "Start test", "Test starten", "开始测试", "テスト開始"},
    {"Testi durdur", "Stop test", "Test stoppen", "停止测试", "テスト停止"},
    {"Tuşlara bastıkça her tuşun derinliği klavye görselinde canlı görünür.",
     "As you press keys, each key's depth is shown live on the keyboard image.",
     "Beim Drücken wird die Tiefe jeder Taste live im Tastaturbild angezeigt.",
     "按下按键时，每个键的深度会实时显示在键盘图上。",
     "キーを押すと、各キーの深さがキーボード画像にリアルタイム表示されます。"},

    // --- Advanced keys tab
    {"Gelişmiş tuşlar", "Advanced keys", "Erweiterte Tasten", "高级按键", "高度なキー"},
    {"Gelişmiş fonksiyon atamak için klavye görselinden tek bir tuş seçin.",
     "Select a single key on the keyboard image to assign an advanced function.",
     "Wählen Sie im Tastaturbild eine einzelne Taste, um eine erweiterte Funktion zuzuweisen.",
     "在键盘图上选择一个键以分配高级功能。", "高度な機能を割り当てるには、キーボード画像でキーを 1 つ選択します。"},
    {"Normal tuş", "Regular key", "Normale Taste", "普通按键", "通常キー"},
    {"4 aşamada 4 ayrı tuş", "4 keys in 4 stages", "4 Tasten in 4 Phasen", "4 个阶段 4 个键", "4 段階で 4 キー"},
    {"3 derinlikte 3 tuş", "3 keys at 3 depths", "3 Tasten in 3 Tiefen", "3 个深度触发 3 个键",
     "3 段階の深さで 3 キー"},
    {"Dokun / basılı tut", "Tap / hold", "Tippen / halten", "轻按 / 长按", "タップ / 長押し"},
    {"Tıkla: basılı kalsın", "Click: stays held", "Klick: bleibt gedrückt", "单击：保持按下", "クリック：押しっぱなし"},
    {"Bırakınca tuş gönder", "Send a key on release", "Taste beim Loslassen senden", "松开时发送按键",
     "離した時にキー送信"},
    {"Derine basan kazanır", "Deeper press wins", "Tiefer gedrückte gewinnt", "按得更深者生效", "深く押した方が優先"},
    {"Karşıt tuş önceliği", "Opposing-key priority", "Gegentasten-Priorität", "对向键优先级", "対向キーの優先"},
    {"Makro", "Macro", "Makro", "宏", "マクロ"},
    {"Eş tuş", "Partner key", "Partnertaste", "配对键", "ペアキー"},
    {"Klavye görselinde eş tuşa tıklayın…", "Click the partner key on the keyboard image…",
     "Klicken Sie im Tastaturbild auf die Partnertaste…", "在键盘图上点击配对键…",
     "キーボード画像でペアキーをクリック…"},
    {"Butona basıp klavyeden seçin", "Click the button, then pick on the keyboard",
     "Schaltfläche klicken, dann auf der Tastatur wählen", "点击按钮后在键盘上选择",
     "ボタンを押してからキーボードで選択"},
    {"1. derinlik", "Depth 1", "Tiefe 1", "深度 1", "深さ 1"},
    {"2. derinlik", "Depth 2", "Tiefe 2", "深度 2", "深さ 2"},
    {"%d. derinlik", "Depth %d", "Tiefe %d", "深度 %d", "深さ %d"},
    {"↓1/↓2: 1./2. derinliğe basış  ·  ↑2/↑1: geri bırakış  ·  ▬: arada basılı tut",
     "↓1/↓2: press to depth 1/2  ·  ↑2/↑1: release  ·  ▬: hold in between",
     "↓1/↓2: bis Tiefe 1/2 drücken  ·  ↑2/↑1: loslassen  ·  ▬: dazwischen halten",
     "↓1/↓2：按到深度 1/2  ·  ↑2/↑1：松开  ·  ▬：期间保持",
     "↓1/↓2：深さ 1/2 まで押す  ·  ↑2/↑1：離す  ·  ▬：その間押し続ける"},
    {"Dokununca", "On tap", "Beim Tippen", "轻按时", "タップ時"},
    {"Basılı tutunca", "On hold", "Beim Halten", "长按时", "長押し時"},
    {"Tuş", "Key", "Taste", "按键", "キー"},
    {"Basılı tutma süresi", "Hold time", "Haltezeit", "长按时间", "長押し時間"},
    {"Bırakınca gönder", "Send on release", "Beim Loslassen senden", "松开时发送", "離した時に送信"},
    {"Son basılan", "Last pressed", "Zuletzt gedrückt", "后按优先", "後押し優先"},
    {"Bu tuş öncelikli", "This key wins", "Diese Taste hat Vorrang", "本键优先", "このキー優先"},
    {"Eş tuş öncelikli", "Partner key wins", "Partnertaste hat Vorrang", "配对键优先", "ペアキー優先"},
    {"Nötr", "Neutral", "Neutral", "中立", "ニュートラル"},
    {"İkisi birlikte basılınca", "When both are pressed", "Wenn beide gedrückt sind", "两键同时按下时",
     "両方押した時"},
    {"Tuş konumuna göre", "By key position", "Nach Tastenposition", "按位置", "キー位置で"},
    {"Tuş koduna göre", "By keycode", "Nach Tastencode", "按键码", "キーコードで"},
    {"Bağlama", "Binding", "Bindung", "绑定方式", "割り当て方式"},
    {"Bu tuşun kodu", "This key's code", "Code dieser Taste", "本键键码", "このキーのコード"},
    {"Eş tuşun kodu", "Partner key's code", "Code der Partnertaste", "配对键键码", "ペアキーのコード"},
    {"Şu anki fonksiyon: %s", "Current function: %s", "Aktuelle Funktion: %s", "当前功能：%s", "現在の機能：%s"},
    {"Bu tuşa makro atanmış; Makrolar sekmesinden yönetilir.",
     "A macro is assigned to this key; manage it in the Macros tab.",
     "Dieser Taste ist ein Makro zugewiesen; verwalten Sie es im Tab „Makros“.", "此键已分配宏，请在“宏”页面管理。",
     "このキーにはマクロが割り当てられています。マクロタブで管理します。"},
    {"Fonksiyonu kaldır", "Remove function", "Funktion entfernen", "移除功能", "機能を解除"},

    // --- Macros tab
    {"Makro kütüphanesi", "Macro library", "Makrobibliothek", "宏库", "マクロライブラリ"},
    {"+ Yeni", "+ New", "+ Neu", "+ 新建", "+ 新規"},
    {"Makro %zu", "Macro %zu", "Makro %zu", "宏 %zu", "マクロ %zu"},
    {"Henüz makro yok.", "No macros yet.", "Noch keine Makros.", "暂无宏。", "マクロはまだありません。"},
    {"%zu adım", "%zu steps", "%zu Schritte", "%zu 步", "%zu ステップ"},
    {"Soldan bir makro seçin veya yeni bir tane oluşturun.", "Select a macro on the left or create a new one.",
     "Wählen Sie links ein Makro oder erstellen Sie ein neues.", "在左侧选择宏或新建一个。",
     "左のマクロを選ぶか、新しく作成してください。"},
    {"Makro adı", "Macro name", "Makroname", "宏名称", "マクロ名"},
    {"● Klavyeden kaydet", "● Record from keyboard", "● Von Tastatur aufnehmen", "● 从键盘录制",
     "● キーボードから記録"},
    {"■ Kaydı durdur", "■ Stop recording", "■ Aufnahme stoppen", "■ 停止录制", "■ 記録停止"},
    {"Kayıt ediliyor… tuşlara basın (%zu / 64 adım)", "Recording… press keys (%zu / 64 steps)",
     "Aufnahme… Tasten drücken (%zu / 64 Schritte)", "正在录制… 请按键（%zu / 64 步）",
     "記録中… キーを押してください（%zu / 64 ステップ）"},
    {"Olay", "Event", "Ereignis", "事件", "イベント"},
    {"Önceki adımdan sonra", "After previous step", "Nach vorherigem Schritt", "距上一步", "前のステップから"},
    {"Bas", "Press", "Drücken", "按下", "押す"},
    {"Bırak", "Release", "Loslassen", "松开", "離す"},
    {"+ Adım ekle", "+ Add step", "+ Schritt hinzufügen", "+ 添加步骤", "+ ステップ追加"},
    {"Süreler milisaniye. Her basışa bir bırakma ekleyin; en fazla 64 adım.",
     "Times are in milliseconds. Add a release for every press; up to 64 steps.",
     "Zeiten in Millisekunden. Zu jedem Drücken ein Loslassen hinzufügen; max. 64 Schritte.",
     "时间单位为毫秒。每次按下都要添加松开；最多 64 步。",
     "時間はミリ秒です。押すごとに離すを追加してください（最大 64 ステップ）。"},
    {"Tuşa ata", "Assign to key", "Taste zuweisen", "分配到按键", "キーに割り当て"},
    {"Bu tuşta makro var", "This key has a macro", "Diese Taste hat ein Makro", "此键已有宏",
     "このキーにはマクロがあります"},
    {"Klavye görselinden tek bir tuş seçin.", "Select a single key on the keyboard image.",
     "Wählen Sie im Tastaturbild eine einzelne Taste.", "在键盘图上选择一个键。",
     "キーボード画像でキーを 1 つ選択します。"},
    {"Çalışma şekli", "Mode", "Modus", "运行方式", "動作モード"},
    {"Bir kez çalıştır", "Run once", "Einmal ausführen", "执行一次", "1回実行"},
    {"Tıkla: tekrarla / durdur", "Click: repeat / stop", "Klick: wiederholen / stoppen", "单击：重复 / 停止",
     "クリック：リピート / 停止"},
    {"Basılıyken tekrarla", "Repeat while held", "Wiederholen solange gehalten", "按住时重复",
     "押している間リピート"},
    {"Basılıyken tekrarla, turu bitir", "Repeat while held, finish run",
     "Wiederholen solange gehalten, Durchlauf beenden", "按住时重复，松开后完成本轮",
     "押している間リピート（最後まで実行）"},
    {"Tekrar sayısı", "Repeat count", "Wiederholungen", "重复次数", "リピート回数"},
    {"Tekrar aralığı (ms)", "Repeat interval (ms)", "Wiederholintervall (ms)", "重复间隔（ms）",
     "リピート間隔（ms）"},
    {"Makroyu tuşa yükle", "Upload macro to key", "Makro auf Taste laden", "将宏写入按键",
     "マクロをキーに書き込む"},
    {"Tuştan makroyu kaldır", "Remove macro from key", "Makro von Taste entfernen", "从按键移除宏",
     "キーからマクロを解除"},
    {"Makro tuşları: %d / 8", "Macro keys: %d / 8", "Makrotasten: %d / 8", "宏按键：%d / 8", "マクロキー：%d / 8"},

    // --- Lighting tab
    {"Ana ışık", "Main light", "Hauptbeleuchtung", "主灯", "メインライト"},
    {"Ortam ışığı", "Ambient light", "Ambientelicht", "氛围灯", "アンビエントライト"},
    {"Statik renk", "Static color", "Statische Farbe", "静态颜色", "固定色"},
    {"Efektler", "Effects", "Effekte", "灯效", "エフェクト"},
    {"Tuş bazlı", "Per key", "Pro Taste", "单键自定义", "キー個別"},
    {"Işık kapalı. Açmak için yukarıdan bir mod seçin.", "Lighting is off. Choose a mode above to turn it on.",
     "Beleuchtung aus. Wählen Sie oben einen Modus.", "灯光已关闭。在上方选择模式以开启。",
     "ライトはオフです。上でモードを選ぶとオンになります。"},
    {"Renk seçin  ·  düzenlemek için çift tıklayın veya sağ tıklayın",
     "Pick a color  ·  double-click or right-click to edit", "Farbe wählen  ·  Doppel- oder Rechtsklick zum Bearbeiten",
     "选择颜色  ·  双击或右键编辑", "色を選択  ·  ダブルクリックまたは右クリックで編集"},
    {"Gökkuşağı: klavyenin kendi renk geçişi.", "Rainbow: the keyboard's built-in color gradient.",
     "Regenbogen: der eingebaute Farbverlauf der Tastatur.", "彩虹：键盘内置渐变色。",
     "レインボー：キーボード内蔵のグラデーション。"},
    {"Renk %d  ·  %s", "Color %d  ·  %s", "Farbe %d  ·  %s", "颜色 %d  ·  %s", "色 %d  ·  %s"},
    {"Efekt seçin", "Choose an effect", "Effekt wählen", "选择灯效", "エフェクトを選択"},
    {"Stil seçin", "Choose a style", "Stil wählen", "选择样式", "スタイルを選択"},
    {"Stil %d", "Style %d", "Stil %d", "样式 %d", "スタイル %d"},
    {"Fırça rengi", "Brush color", "Pinselfarbe", "画笔颜色", "ブラシの色"},
    {"Özel renk", "Custom color", "Eigene Farbe", "自定义颜色", "カスタム色"},
    {"Seçili %zu tuşa uygula", "Apply to %zu selected keys", "Auf %zu ausgewählte Tasten anwenden",
     "应用到已选的 %zu 个键", "選択した %zu キーに適用"},
    {"Tüm klavyeyi boya", "Paint whole keyboard", "Ganze Tastatur färben", "全部按键着色", "キーボード全体を塗る"},
    {"Tuştan renk al", "Pick color from key", "Farbe von Taste übernehmen", "从按键取色", "キーから色を取得"},
    {"Klavye görselinde tuşları seçin (Ctrl ile çoklu seçim), sonra rengi uygulayın.",
     "Select keys on the keyboard image (Ctrl for multiple), then apply the color.",
     "Wählen Sie Tasten im Tastaturbild (Strg für mehrere) und wenden Sie dann die Farbe an.",
     "在键盘图上选择按键（按住 Ctrl 多选），然后应用颜色。",
     "キーボード画像でキーを選択（Ctrl で複数）し、色を適用します。"},
    {"Parlaklık", "Brightness", "Helligkeit", "亮度", "明るさ"},
    {"Hız", "Speed", "Geschwindigkeit", "速度", "速度"},
    {"Yön", "Direction", "Richtung", "方向", "方向"},
    {"Uykuya geç", "Sleep after", "Ruhezustand nach", "休眠时间", "スリープまで"},
    {"%d dakika sonra", "After %d minutes", "Nach %d Minuten", "%d 分钟后", "%d 分後"},
    {"Hiçbir zaman", "Never", "Nie", "从不", "しない"},
    {"Dalgalanma", "Ripple", "Welle", "涟漪", "リップル"},
    {"Gelgit", "Tide", "Gezeiten", "潮汐", "潮の満ち引き"},
    {"Hafif dalga", "Gentle ripple", "Sanfte Welle", "轻漾", "さざ波"},
    {"Dönen fırtına", "Spinning storm", "Wirbelsturm", "旋转风暴", "回転ストーム"},
    {"Şanslı gökkuşağı", "Lucky rainbow", "Glücksregenbogen", "幸运彩虹", "ラッキーレインボー"},
    {"Parlayan gökkuşağı", "Shining rainbow", "Leuchtender Regenbogen", "闪耀彩虹", "輝く虹"},
    {"Pırıltı", "Sparkle", "Funkeln", "熠熠生辉", "きらめき"},
    {"Kayan pencere", "Moving pane", "Laufendes Fenster", "移动窗格", "移動ペイン"},
    {"Dalga formu", "Waveform", "Wellenform", "波形变换", "波形"},
    {"Kayan gölgeler", "Shifting shadows", "Wandernde Schatten", "移形换影", "移ろう影"},
    {"Sinüs", "Sine wave", "Sinuswelle", "正弦曲线", "サイン波"},
    {"Akan bulutlar", "Flowing clouds", "Ziehende Wolken", "行云流水", "流れる雲"},
    {"Çiçekler", "Blossoms", "Blüten", "百花争艳", "花々"},
    {"Renk mozaiği", "Color mosaic", "Farbmosaik", "斑斓镶嵌", "カラーモザイク"},
    {"Yağmur", "Rainfall", "Regen", "雨落如注", "雨"},
    {"Durmayan hareket", "Ceaseless motion", "Ständige Bewegung", "跃动不息", "躍動"},
    {"Kar izi", "Snow trail", "Schneespur", "踏雪无痕", "雪の足跡"},
    {"Kar ve erik", "Snow plum", "Schnee und Pflaume", "踏雪寻梅", "雪中梅"},
    {"Lazer", "Laser", "Laser", "镭射穿云", "レーザー"},
    {"Su dalgası", "Water ripple", "Wasserwelle", "水波荡漾", "水の波紋"},

    // --- Device tab
    {"Cihaz bilgisi", "Device info", "Geräteinfo", "设备信息", "デバイス情報"},
    {"Ad", "Name", "Name", "名称", "名前"},
    {"Protokol", "Protocol", "Protokoll", "协议", "プロトコル"},
    {"Seri no", "Serial no.", "Seriennr.", "序列号", "シリアル番号"},
    {"Hassasiyet", "Precision", "Genauigkeit", "精度", "精度"},
    {"%.3f mm adım  ·  en fazla %.2f mm", "%.3f mm step  ·  max %.2f mm", "%.3f mm Schritt  ·  max. %.2f mm",
     "步进 %.3f mm  ·  最大 %.2f mm", "%.3f mm 刻み  ·  最大 %.2f mm"},
    {"Hz. Değiştirince klavye yeniden başlar; program otomatik yeniden bağlanır.",
     "Hz. The keyboard restarts after a change; the app reconnects automatically.",
     "Hz. Nach einer Änderung startet die Tastatur neu; die App verbindet sich automatisch.",
     "Hz。更改后键盘会重启，程序会自动重新连接。", "Hz。変更するとキーボードが再起動し、アプリは自動で再接続します。"},
    {"İşletim sistemi", "Operating system", "Betriebssystem", "操作系统", "OS"},
    {"Fabrika ayarları", "Factory reset", "Werkseinstellungen", "恢复出厂设置", "工場出荷時設定"},
    {"Tuş atamaları, performans, gelişmiş tuşlar ve ışık ayarları klavyenin varsayılanlarına döner.",
     "Keymap, performance, advanced keys and lighting return to the keyboard's defaults.",
     "Belegung, Performance, erweiterte Tasten und Beleuchtung werden auf die Standardwerte zurückgesetzt.",
     "按键映射、性能、高级按键和灯光将恢复为键盘默认值。",
     "キー割り当て、パフォーマンス、高度なキー、ライティングがキーボードの初期値に戻ります。"},
    {"Fabrika ayarlarına dön…", "Reset to factory settings…", "Auf Werkseinstellungen zurücksetzen…",
     "恢复出厂设置…", "工場出荷時に戻す…"},
    {"Emin misiniz? Bu işlem geri alınamaz.", "Are you sure? This cannot be undone.",
     "Sind Sie sicher? Dies kann nicht rückgängig gemacht werden.", "确定吗？此操作无法撤销。",
     "よろしいですか？この操作は元に戻せません。"},
    {"Evet, sıfırla", "Yes, reset", "Ja, zurücksetzen", "是，重置", "はい、リセット"},
    {"Kalibrasyon", "Calibration", "Kalibrierung", "校准", "キャリブレーション"},
    {"Tuşlar yanlış algılanıyorsa kalibrasyon yapın: başlattıktan sonra her tuşa sonuna kadar basıp bırakın. "
     "Tamamlanan tuşlar klavye görselinde yeşile döner.",
     "Calibrate if keys are detected incorrectly: after starting, press every key all the way down and release "
     "it. Finished keys turn green on the keyboard image.",
     "Kalibrieren Sie, wenn Tasten falsch erkannt werden: Nach dem Start jede Taste ganz durchdrücken und "
     "loslassen. Fertige Tasten werden im Tastaturbild grün.",
     "如果按键识别不准确请进行校准：开始后将每个键按到底再松开，完成的键会在键盘图上变绿。",
     "キーが正しく検出されない場合はキャリブレーションを行います。開始後、すべてのキーを底まで押して離してください。"
     "完了したキーはキーボード画像で緑になります。"},
    {"Kalibrasyonu başlat", "Start calibration", "Kalibrierung starten", "开始校准", "キャリブレーション開始"},
    {"%d / %d tuş", "%d / %d keys", "%d / %d Tasten", "%d / %d 个键", "%d / %d キー"},
    {"Kaydet ve bitir", "Save and finish", "Speichern und beenden", "保存并完成", "保存して終了"},

    // --- About / versions
    {"Hakkında", "About", "Über", "关于", "このアプリについて"},
    {"Sürüm", "Version", "Version", "版本", "バージョン"},
    {"Sürüm notları", "Release notes", "Versionshinweise", "更新说明", "リリースノート"},
    {"GitHub sayfasını aç", "Open GitHub page", "GitHub-Seite öffnen", "打开 GitHub 页面", "GitHub ページを開く"},
    {"Resmi olmayan, açık kaynak yapılandırma programı. Chilkey ile bağlantılı değildir.",
     "Unofficial open-source configurator. Not affiliated with Chilkey.",
     "Inoffizielles Open-Source-Konfigurationsprogramm. Nicht mit Chilkey verbunden.",
     "非官方开源配置工具，与 Chilkey 无关。", "非公式のオープンソース設定ツールです。Chilkey とは無関係です。"},
    {"Arayüz dili: Türkçe, English, Deutsch, 中文, 日本語", "UI languages: Türkçe, English, Deutsch, 中文, 日本語",
     "Sprachen: Türkçe, English, Deutsch, 中文, 日本語", "界面语言：Türkçe、English、Deutsch、中文、日本語",
     "表示言語：Türkçe、English、Deutsch、中文、日本語"},
    {"Yeni klavye görseli ve tüm menülerde yeni tasarım", "New keyboard visual and redesigned menus",
     "Neue Tastaturdarstellung und überarbeitete Menüs", "全新键盘图和重新设计的菜单",
     "新しいキーボード表示とメニューの再デザイン"},
    {"Sürüm bilgisi ve Hakkında bölümü", "Version info and About section", "Versionsinfo und Über-Bereich",
     "版本信息与关于页面", "バージョン情報と「このアプリについて」"},
    {"Gerçek klavyede doğrulanan yazma işlemleri", "Write operations verified on a real keyboard",
     "Schreibvorgänge an einer echten Tastatur geprüft", "写入操作已在真实键盘上验证",
     "書き込み操作を実機で検証"},
    {"İlk sürüm", "First release", "Erste Version", "首个版本", "初回リリース"},
};

Lang g_lang = Lang::EN;

const char* Pick(const Entry& e, Lang lang) {
    const char* s = nullptr;
    switch (lang) {
        case Lang::TR: s = e.tr; break;
        case Lang::EN: s = e.en; break;
        case Lang::DE: s = e.de; break;
        case Lang::ZH: s = e.zh; break;
        case Lang::JA: s = e.ja; break;
        default: break;
    }
    return s ? s : e.en;
}

const std::unordered_map<std::string_view, const Entry*>& Index() {
    static const auto index = [] {
        std::unordered_map<std::string_view, const Entry*> m;
        for (const Entry& e : kEntries) m.emplace(e.tr, &e);
        return m;
    }();
    return index;
}

const char* const kNames[] = {"Türkçe", "English", "Deutsch", "中文", "日本語"};
const char* const kCodes[] = {"tr", "en", "de", "zh", "ja"};

}  // namespace

void SetLanguage(Lang lang) { g_lang = lang; }
Lang GetLanguage() { return g_lang; }

Lang DefaultLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_TURKISH: return Lang::TR;
        case LANG_GERMAN: return Lang::DE;
        case LANG_CHINESE: return Lang::ZH;
        case LANG_JAPANESE: return Lang::JA;
        default: return Lang::EN;
    }
}

const char* LangName(Lang lang) { return kNames[static_cast<int>(lang)]; }
const char* LangCode(Lang lang) { return kCodes[static_cast<int>(lang)]; }

bool LangFromCode(const std::string& code, Lang* out) {
    for (int i = 0; i < static_cast<int>(Lang::Count); ++i) {
        if (code == kCodes[i]) {
            *out = static_cast<Lang>(i);
            return true;
        }
    }
    return false;
}

const char* T(const char* text) {
    if (!text) return text;
    const auto& index = Index();
    auto it = index.find(text);
    return it == index.end() ? text : Pick(*it->second, g_lang);
}

std::string I18nGlyphText() {
    std::string all;
    for (const char* n : kNames) all += n;
    for (const Entry& e : kEntries) all += Pick(e, g_lang);
    return all;
}

const char* I18nCjkFontPath() {
    // Chinese and Japanese need their own fonts; the language names alone fit in either.
    static const char* const kZh[] = {"C:\\Windows\\Fonts\\msyh.ttc", "C:\\Windows\\Fonts\\simsun.ttc"};
    static const char* const kJa[] = {"C:\\Windows\\Fonts\\YuGothM.ttc", "C:\\Windows\\Fonts\\meiryo.ttc",
                                      "C:\\Windows\\Fonts\\msgothic.ttc"};
    auto first = [](const char* const* list, size_t n) -> const char* {
        for (size_t i = 0; i < n; ++i) {
            if (GetFileAttributesA(list[i]) != INVALID_FILE_ATTRIBUTES) return list[i];
        }
        return nullptr;
    };
    if (g_lang == Lang::JA) {
        if (const char* f = first(kJa, 3)) return f;
    }
    if (const char* f = first(kZh, 2)) return f;
    return first(kJa, 3);
}
