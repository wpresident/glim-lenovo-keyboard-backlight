// Glim — tray utility that keeps the keyboard backlight mode on Lenovo laptops (Xiaoxin, IdeaPad, ThinkBook via \\.\EnergyDrv,
// ThinkPad via \\.\IBMPmDrv)
// Build: see build.sh
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <stdint.h>
#include <wchar.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <winhttp.h>
#include "version.h"

#define IOCTL_BACKLIGHT 0x83102144
#define APP_VERSION L"" GLIM_VER_STR
/* the same number as FILEVERSION in res.rc: major.minor.patch.0 */
#define APP_FILEVER ((((ULONGLONG)GLIM_VER_MAJOR << 16 | GLIM_VER_MINOR) << 32) | ((ULONGLONG)GLIM_VER_PATCH << 16))
#define APP_URL L"https://github.com/wpresident/glim-lenovo-keyboard-backlight"
#define APP_CLASS L"GlimWnd"
#define REG_KEY   L"Software\\Glim"
#define RUN_KEY   L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_NAME  L"Glim"
/* GlimWnd, the mutex Local\\Glim_SingleInstance, WM_SHOWME, WM_GLIM_VERSION, WM_GLIM_SELFTEST and ID_TRAY_EXIT (205)
   are how copies of different versions talk to each other: never change them */
#define WM_TRAY   (WM_APP + 1)
#define WM_SHOWME (WM_APP + 2)
#define WM_GLIM_VERSION  (WM_APP + 3)  /* returns APP_FILEVER */
#define WM_GLIM_SELFTEST (WM_APP + 4)  /* wParam = quiet; returns 1 = all OK, 2 = problems found */
#define WM_GLIM_TEST_DISPLAY (WM_APP + 5)  /* test runs only (GLIM_FAKE): wParam = console display state */
#define WM_GLIM_UPD_INFO (WM_APP + 6)  /* update check finished: lParam = upd_t* */
#define WM_GLIM_TEST_STATE (WM_APP + 7)  /* test runs only (GLIM_FAKE): wParam 0 = tray icon (0..3 mode, 4 app icon), 1 = dark, 2 = DPI,
                                             3 = check boxes/radios drawn by draw_check, 4 = font height (pixels),
                                             5 = bold controls (bits 0-3 radios, 4-6 check boxes) + 128 if Apply is enabled */
#define WM_GLIM_TEST_DPI   (WM_APP + 8)  /* test runs only (GLIM_FAKE): behave as if moved to a monitor with this DPI (wParam) */
#define UPD_API    L"https://api.github.com/repos/wpresident/glim-lenovo-keyboard-backlight/releases/latest"

enum { ID_R0 = 100, ID_R1, ID_R2, ID_R3, ID_AUTOSTART, ID_ONWAKE, ID_APPLY, ID_HIDE, ID_STATUS, ID_TEST,
       ID_GROUP, ID_LANGLBL, ID_LANG, ID_RESPECT, ID_ABOUT, ID_UPDATE,
       ID_TRAY_OPEN = 200, ID_TRAY_OFF, ID_TRAY_LOW, ID_TRAY_HIGH, ID_TRAY_AUTO, ID_TRAY_EXIT = 205, ID_TRAY_RESTORE, ID_TRAY_ABOUT, ID_TRAY_UPDATE,
       T_START1 = 300, T_START2, T_START3, T_WAKE, T_POLL, T_TRAY };

/* ---------- DLLs that are not KnownDLLs: loaded from System32 only, never from the exe's folder ----------
   (an exe in Downloads would otherwise pick up a planted VERSION.dll / WINHTTP.dll) */
static __typeof__(&GetFileVersionInfoSizeW) p_GetFileVersionInfoSizeW;
static __typeof__(&GetFileVersionInfoW) p_GetFileVersionInfoW;
static __typeof__(&VerQueryValueW) p_VerQueryValueW;
static __typeof__(&WinHttpCrackUrl) p_WinHttpCrackUrl;
static __typeof__(&WinHttpOpen) p_WinHttpOpen;
static __typeof__(&WinHttpSetTimeouts) p_WinHttpSetTimeouts;
static __typeof__(&WinHttpConnect) p_WinHttpConnect;
static __typeof__(&WinHttpOpenRequest) p_WinHttpOpenRequest;
static __typeof__(&WinHttpAddRequestHeaders) p_WinHttpAddRequestHeaders;
static __typeof__(&WinHttpSendRequest) p_WinHttpSendRequest;
static __typeof__(&WinHttpReceiveResponse) p_WinHttpReceiveResponse;
static __typeof__(&WinHttpQueryHeaders) p_WinHttpQueryHeaders;
static __typeof__(&WinHttpQueryDataAvailable) p_WinHttpQueryDataAvailable;
static __typeof__(&WinHttpReadData) p_WinHttpReadData;
static __typeof__(&WinHttpCloseHandle) p_WinHttpCloseHandle;
static __typeof__(&SetWindowTheme) p_SetWindowTheme;
static __typeof__(&OpenThemeData) p_OpenThemeData;
static __typeof__(&CloseThemeData) p_CloseThemeData;
static __typeof__(&DrawThemeBackground) p_DrawThemeBackground;
static __typeof__(&GetThemePartSize) p_GetThemePartSize;
static __typeof__(&DwmSetWindowAttribute) p_DwmSetWindowAttribute;
static int g_haveVersion, g_haveHttp, g_haveTheme;
/* newer than Windows 8.1, or not documented: taken only if present (and the undocumented ones only on builds that have them) */
static HTHEME (WINAPI *p_OpenThemeDataForDpi)(HWND, LPCWSTR, UINT);
static BOOL (WINAPI *p_AllowDarkModeForWindow)(HWND, BOOL);  /* uxtheme #133, Windows 10 1809+ */
static int (WINAPI *p_SetPreferredAppMode)(int);            /* uxtheme #135, 1903+ (1809: AllowDarkModeForApp(BOOL), same values) */
static void (WINAPI *p_FlushMenuThemes)(void);              /* uxtheme #136, 1809+ */
static UINT (WINAPI *p_GetDpiForWindow)(HWND);
static int (WINAPI *p_GetSystemMetricsForDpi)(int, UINT);
static BOOL (WINAPI *p_SystemParametersInfoForDpi)(UINT, UINT, PVOID, UINT, UINT);
static BOOL (WINAPI *p_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
static DWORD g_build;  /* Windows build number (RtlGetVersion: not affected by the manifest) */
static HMODULE g_uxtheme;
#define LOAD_FN(m, f) (p_##f = (__typeof__(&f))(void (*)(void))GetProcAddress(m, #f))
#define LOAD_ANY(m, p, name) ((p) = (__typeof__(p))(void (*)(void))GetProcAddress(m, name))
static void load_dlls(void) {
    HMODULE m;
    if ((m = GetModuleHandleW(L"ntdll.dll")) != NULL) {
        LONG (WINAPI *rtl_ver)(OSVERSIONINFOW*) = NULL; OSVERSIONINFOW vi = { sizeof vi };
        if (LOAD_ANY(m, rtl_ver, "RtlGetVersion") && rtl_ver(&vi) == 0) g_build = vi.dwBuildNumber;
    }
    if ((m = GetModuleHandleW(L"user32.dll")) != NULL) {
        LOAD_ANY(m, p_GetDpiForWindow, "GetDpiForWindow"); LOAD_ANY(m, p_GetSystemMetricsForDpi, "GetSystemMetricsForDpi");
        LOAD_ANY(m, p_SystemParametersInfoForDpi, "SystemParametersInfoForDpi"); LOAD_ANY(m, p_AdjustWindowRectExForDpi, "AdjustWindowRectExForDpi");
    }
    if ((m = LoadLibraryExW(L"uxtheme.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL) {
        g_haveTheme = LOAD_FN(m, SetWindowTheme) && LOAD_FN(m, OpenThemeData) && LOAD_FN(m, CloseThemeData) && LOAD_FN(m, DrawThemeBackground) && LOAD_FN(m, GetThemePartSize);
        LOAD_ANY(m, p_OpenThemeDataForDpi, "OpenThemeDataForDpi");
        g_uxtheme = m;
    }
    if ((m = LoadLibraryExW(L"dwmapi.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL) LOAD_FN(m, DwmSetWindowAttribute);
    if ((m = LoadLibraryExW(L"VERSION.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL)
        g_haveVersion = LOAD_FN(m, GetFileVersionInfoSizeW) && LOAD_FN(m, GetFileVersionInfoW) && LOAD_FN(m, VerQueryValueW);
}
/* the network DLL: only when the user asks for an update - Glim doesn't touch the network otherwise */
static void load_net_dlls(void) {
    static int done; HMODULE m;
    if (done) return;
    done = 1;
    if ((m = LoadLibraryExW(L"WINHTTP.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32)) != NULL)
        g_haveHttp = LOAD_FN(m, WinHttpCrackUrl) && LOAD_FN(m, WinHttpOpen) && LOAD_FN(m, WinHttpSetTimeouts) && LOAD_FN(m, WinHttpConnect) && LOAD_FN(m, WinHttpOpenRequest) && LOAD_FN(m, WinHttpAddRequestHeaders) && LOAD_FN(m, WinHttpSendRequest) && LOAD_FN(m, WinHttpReceiveResponse) && LOAD_FN(m, WinHttpQueryHeaders) && LOAD_FN(m, WinHttpQueryDataAvailable) && LOAD_FN(m, WinHttpReadData) && LOAD_FN(m, WinHttpCloseHandle);
}

/* ---------- localization ---------- */
enum { L_EN, L_RU, L_ZH, L_COUNT };
enum { S_MODE0, S_MODE1, S_MODE2, S_MODE3, S_GROUP, S_AUTOSTART, S_ONWAKE, S_TEST, S_APPLY, S_HIDE,
       S_LANG, S_CUR, S_ERR, S_OPEN, S_EXIT, S_TIP, S_E_MISSING, S_E_DENIED, S_E_UNSUP, S_BALLOON,
       S_DRV_TITLE, S_DRV_MISSING, S_DRV_DENIED,
       S_T_TITLE, S_T_DRVFAIL, S_T_DRVOK, S_T_SETFAIL, S_T_SETWARN, S_T_SETOK, S_T_RESTOK, S_T_RESTFAIL,
       S_T_REGOK, S_T_REGFAIL, S_T_AUTOOK, S_T_AUTOFAIL, S_T_AUTOOFF, S_T_SUM, S_T_GOOD, S_T_BAD, S_T_LOG,
       S_RESPECT, S_CUR_MANUAL, S_RESTORE, S_ABOUT, S_ABOUT_TITLE, S_ABOUT_TEXT, S_ABOUT_FOOT, S_TAKEOVER_FAIL, S_OTHER_RIGHTS,
       S_UPD_BUTTON, S_UPD_CHECKING, S_UPD_TITLE, S_UPD_LATEST, S_UPD_ASK, S_UPD_FAIL, S_UPD_E_NET, S_UPD_E_FORMAT,
       S_UPD_E_LIMIT, S_UPD_E_NOFILE, S_AUTO_GONE, S_AUTO_OLDER, S_AUTO_TEMP, S_E_NOMODE, S_T_RAW, S_T_NOMODE, S_E_REFUSED, S_T_NOEFFECT, S_COUNT };

static const wchar_t *STR[L_COUNT][S_COUNT] = {
 { /* English */
  L"Off", L"Low", L"High", L"Auto", L"Backlight mode", L"Run at Windows startup",
  L"Force re-apply after sleep/hibernation", L"Self-test", L"Apply", L"Hide", L"Language:",
  L"Current: %ls", L"Error: %ls", L"Open settings", L"Exit", L"Backlight: %ls",
  L"Lenovo Energy Management (or, on ThinkPads, Power Management) driver not found", L"no access to the driver (administrator rights required)",
  L"the driver doesn't support backlight control", L"Failed to set backlight: %ls",
  L"Glim — driver check",
  L"No Lenovo driver for the keyboard backlight was found: neither Lenovo Energy Management (\\\\.\\EnergyDrv: IdeaPad, Xiaoxin, ThinkBook) nor Lenovo Power Management (\\\\.\\IBMPmDrv: ThinkPad).\n\nInstall the one for your model from the Lenovo support site (for Chinese models: newsupport.lenovo.com.cn, \"Power management\" section), restart the PC and run Glim again.",
  L"Access to the Lenovo driver was denied.\n\nTry running Glim as administrator once and run the self-test.",
  L"Glim — self-test",
  L"[FAIL] Driver: %ls (code %lu)", L"[ OK ] Driver available, current mode: %ls",
  L"[FAIL] Setting \"%ls\" failed", L"[WARN] \"%ls\": set, but driver reported %d", L"[ OK ] \"%ls\": set and confirmed",
  L"[ OK ] Mode restored: %ls", L"[FAIL] Couldn't restore mode: %ls",
  L"[ OK ] Settings are saved in the registry", L"[FAIL] Settings can't be read from the registry",
  L"[ OK ] Autostart: %ls", L"[FAIL] Autostart points to a missing file", L"[INFO] Autostart is off",
  L"Result: %ls (errors %d, warnings %d)", L"everything works", L"PROBLEMS FOUND", L"Log: %ls\\log.txt",
  L"Keep manual changes (Fn+Space) until restart", L"Current: %ls (manual, until restart)", L"Restore saved mode (%ls)",
  L"About", L"About Glim",
  L"Glim keeps your keyboard backlight mode on Lenovo laptops (Xiaoxin, IdeaPad, ThinkBook, ThinkPad).\n\nMany models reset the backlight (to Auto, or off) after every boot or hibernation, some also after sleep or when the screen turns off. Glim restores the mode you chose: Off, Low, High or Auto (where the keyboard has it).",
  L"Free and open source (MIT). Not affiliated with Lenovo.",
  L"Couldn't close the running copy of Glim.\n\nExit Glim from the tray menu and run this file again.",
  L"Glim is already running with different rights (as administrator or not).\n\nExit it from the tray menu and run this file again.",
  L"Check for updates", L"Checking…", L"Glim — updates", L"You have the latest version: %ls.",
  L"Glim %ls is available (you have %ls).\n\n%ls\n\nOpen the download page? To update: exit Glim (tray menu \x2192 Exit), save the new Glim.exe in place of this one:\n%ls\nand start it. Settings and autostart are kept.",
  L"Couldn't check for updates: %ls.", L"no connection to GitHub", L"unexpected answer from GitHub",
  L"GitHub limits the number of requests, try again in an hour", L"the release has no Glim.exe yet",
  L"Autostart is off: the Glim.exe it started is gone (moved, deleted or removed by an antivirus). Turn it on again in the settings.",
  L"Autostart still starts an older Glim. Open the settings and click Apply to start this one instead.",
  L"Autostart is not turned on: Glim.exe is in a folder for temporary files (%ls), and autostart would stop working when it is deleted. Exit Glim (tray menu \x2192 Exit), move Glim.exe to a permanent folder (for example C:\\Glim), start it from there, tick the box and click Apply.",
  L"this keyboard doesn't have this mode", L"[INFO] Driver %ls: %ls", L"[INFO] \"%ls\": this keyboard doesn't have this mode",
  L"the driver didn't accept the change", L"[FAIL] The driver accepts the modes, but the backlight doesn't change" },
 { /* Russian */
  L"Выключена", L"Слабая", L"Яркая", L"Авто", L"Режим подсветки", L"Запускать вместе с Windows",
  L"Принудительно применять после сна/гибернации", L"Самотест", L"Применить", L"Скрыть", L"Язык:",
  L"Сейчас: %ls", L"Ошибка: %ls", L"Открыть настройки", L"Выход", L"Подсветка: %ls",
  L"драйвер Lenovo Energy Management (или Power Management на ThinkPad) не найден", L"нет доступа к драйверу (нужны права администратора)",
  L"драйвер не поддерживает управление подсветкой", L"Не удалось установить подсветку: %ls",
  L"Glim — проверка драйвера",
  L"Не найден драйвер Lenovo для подсветки клавиатуры: ни Lenovo Energy Management (\\\\.\\EnergyDrv: IdeaPad, Xiaoxin, ThinkBook), ни Lenovo Power Management (\\\\.\\IBMPmDrv: ThinkPad).\n\nУстановите драйвер для вашей модели с сайта поддержки Lenovo (для китайских моделей: newsupport.lenovo.com.cn → 电源管理), перезагрузите ПК и снова запустите Glim.",
  L"Нет доступа к драйверу Lenovo.\n\nПопробуйте один раз запустить Glim от имени администратора и выполнить самотест.",
  L"Glim — самотест",
  L"[FAIL] Драйвер: %ls (код %lu)", L"[ OK ] Драйвер доступен, текущий режим: %ls",
  L"[FAIL] Установка «%ls» не выполнена", L"[WARN] «%ls»: установлено, но драйвер вернул %d", L"[ OK ] «%ls»: установлено и подтверждено",
  L"[ OK ] Режим возвращён: %ls", L"[FAIL] Не удалось вернуть режим: %ls",
  L"[ OK ] Настройки сохраняются в реестре", L"[FAIL] Настройки не читаются из реестра",
  L"[ OK ] Автозапуск: %ls", L"[FAIL] Автозапуск указывает на несуществующий файл", L"[INFO] Автозапуск выключен",
  L"Итог: %ls (ошибок %d, предупреждений %d)", L"всё работает", L"ЕСТЬ ПРОБЛЕМЫ", L"Лог: %ls\\log.txt",
  L"Учитывать ручное переключение (Fn+Пробел) до перезагрузки", L"Сейчас: %ls (вручную, до перезагрузки)", L"Вернуть сохранённый режим (%ls)",
  L"О программе", L"О программе Glim",
  L"Glim сохраняет выбранный режим подсветки клавиатуры на ноутбуках Lenovo (Xiaoxin, IdeaPad, ThinkBook, ThinkPad).\n\nМногие модели после каждой загрузки или гибернации включают «Авто» или выключают подсветку, некоторые — и после сна или выключения экрана. Glim возвращает ваш режим: выключена, слабая, яркая или авто (если такой режим есть у клавиатуры).",
  L"Бесплатная программа с открытым кодом (MIT). Не связана с Lenovo.",
  L"Не удалось закрыть уже запущенный Glim.\n\nВыйдите из Glim через меню в трее и запустите этот файл ещё раз.",
  L"Glim уже запущен с другими правами (от имени администратора или без них).\n\nВыйдите из него через меню в трее и запустите этот файл ещё раз.",
  L"Проверить обновления", L"Проверка…", L"Glim — обновления", L"У вас последняя версия: %ls.",
  L"Доступна версия Glim %ls (у вас %ls).\n\n%ls\n\nОткрыть страницу загрузки? Чтобы обновиться: выйдите из Glim (меню в трее \x2192 Выход), сохраните новый Glim.exe вместо этого:\n%ls\nи запустите его. Настройки и автозапуск сохранятся.",
  L"Не удалось проверить обновления: %ls.", L"нет связи с GitHub", L"неожиданный ответ GitHub",
  L"GitHub ограничил число запросов, попробуйте через час", L"в релизе ещё нет Glim.exe",
  L"Автозапуск выключен: файл Glim.exe, который он запускал, пропал (перемещён или удалён, например антивирусом). Включите его снова в настройках.",
  L"Автозапуск всё ещё запускает старую версию Glim. Откройте настройки и нажмите «Применить», чтобы запускалась эта.",
  L"Автозапуск не включён: Glim.exe лежит в папке для временных файлов (%ls), и когда его удалят, автозапуск перестанет работать. Выйдите из Glim (меню в трее \x2192 Выход), перенесите Glim.exe в постоянную папку (например, C:\\Glim), запустите оттуда, поставьте галочку и нажмите «Применить».",
  L"у этой клавиатуры нет такого режима", L"[INFO] Драйвер %ls: %ls", L"[INFO] «%ls»: у этой клавиатуры нет такого режима",
  L"драйвер не принял изменение", L"[FAIL] Драйвер принимает режимы, но подсветка не меняется" },
 { /* Chinese */
  L"关闭", L"低亮", L"高亮", L"自动", L"键盘背光模式", L"开机时自动启动",
  L"睡眠/休眠后强制重新应用", L"自检", L"应用", L"隐藏", L"语言：",
  L"当前：%ls", L"错误：%ls", L"打开设置", L"退出", L"背光：%ls",
  L"未找到联想电源管理驱动 (Lenovo Energy Management，ThinkPad 为 Lenovo Power Management)", L"无权访问驱动（需要管理员权限）",
  L"该驱动不支持键盘背光控制", L"无法设置背光：%ls",
  L"Glim — 驱动检查",
  L"未找到联想键盘背光驱动：联想电源管理驱动 (\\\\.\\EnergyDrv，适用于小新、IdeaPad、ThinkBook) 和 ThinkPad 电源管理驱动 (\\\\.\\IBMPmDrv) 都不存在。\n\n请在联想服务网站 newsupport.lenovo.com.cn 为您的机型安装相应的驱动，重启电脑后再运行 Glim。",
  L"无权访问联想电源管理驱动。\n\n请尝试以管理员身份运行一次 Glim 并执行自检。",
  L"Glim — 自检",
  L"[FAIL] 驱动：%ls（代码 %lu）", L"[ OK ] 驱动可用，当前模式：%ls",
  L"[FAIL] 设置“%ls”失败", L"[WARN] “%ls”：已设置，但驱动返回 %d", L"[ OK ] “%ls”：已设置并确认",
  L"[ OK ] 已恢复模式：%ls", L"[FAIL] 无法恢复模式：%ls",
  L"[ OK ] 设置已保存到注册表", L"[FAIL] 无法从注册表读取设置",
  L"[ OK ] 开机启动：%ls", L"[FAIL] 开机启动指向的文件不存在", L"[INFO] 开机启动已关闭",
  L"结果：%ls（错误 %d，警告 %d）", L"一切正常", L"发现问题", L"日志：%ls\\log.txt",
  L"保留手动切换（Fn+空格）直到重启", L"当前：%ls（手动，直到重启）", L"恢复已保存的模式（%ls）",
  L"关于", L"关于 Glim",
  L"Glim 为联想笔记本（小新、IdeaPad、ThinkBook、ThinkPad）保持您选择的键盘背光模式。\n\n许多机型在每次开机或休眠后都会重置背光（变为自动或关闭），有些在睡眠或屏幕关闭后也会。Glim 会恢复您选择的模式：关闭、低亮、高亮或自动（如果键盘支持）。",
  L"免费开源软件 (MIT)。与联想无关。",
  L"无法关闭正在运行的 Glim。\n\n请从托盘菜单退出 Glim，然后再次运行此文件。",
  L"Glim 已经以不同的权限（管理员或非管理员）运行。\n\n请从托盘菜单退出它，然后再次运行此文件。",
  L"检查更新", L"正在检查…", L"Glim — 更新", L"您使用的已是最新版本：%ls。",
  L"Glim %ls 已发布（您当前的版本是 %ls）。\n\n%ls\n\n打开下载页面吗？更新方法：退出 Glim（托盘菜单 \x2192 退出），用新的 Glim.exe 替换此文件：\n%ls\n然后启动它。设置和开机启动会保留。",
  L"无法检查更新：%ls。", L"无法连接到 GitHub", L"GitHub 返回了意外的结果",
  L"GitHub 限制了请求次数，请一小时后再试", L"该版本尚未包含 Glim.exe",
  L"开机启动已关闭：它启动的 Glim.exe 已不存在（被移动、删除或被杀毒软件删除）。请在设置中重新开启。",
  L"开机启动仍在启动旧版 Glim。请打开设置并点击“应用”，改为启动此版本。",
  L"开机启动未开启：Glim.exe 位于临时文件夹（%ls），删除后开机启动将失效。请退出 Glim（托盘菜单 \x2192 退出），将 Glim.exe 移到固定的文件夹（例如 C:\\Glim），从那里启动，勾选后点击“应用”。",
  L"此键盘没有该模式", L"[INFO] 驱动 %ls：%ls", L"[INFO] “%ls”：此键盘没有该模式",
  L"驱动未接受此更改", L"[FAIL] 驱动接受了各模式，但背光没有变化" }
};
static const wchar_t *LANG_NAMES[L_COUNT] = { L"English", L"Русский", L"中文" };
static const wchar_t *LOG_MODE[4] = { L"off", L"low", L"high", L"auto" };

static HINSTANCE g_inst; static HWND g_wnd; static int g_dpi = 96;
static HFONT g_font, g_fontUi, g_fontZh;  /* current; system message font; Chinese UI font (NULL if not installed) */
static HFONT g_fontBold, g_fontUiB, g_fontZhB;  /* the same in bold: settings changed but not applied yet */
static HICON g_smallIcon, g_trayIcon[4];  /* app icon (16 px design); tray icons by backlight mode (res.rc 2..5) */
static int g_trayLevel = -1;   /* the level the tray icon shows; -1 = unknown: app icon */
static HICON g_trayShown;      /* the icon and tooltip the taskbar accepted last (NULL = none yet / to be re-sent) */
static wchar_t g_trayTipShown[128];
static int g_dark = 0;         /* the window follows Windows' dark app mode */
static int g_customDraws = 0;  /* check boxes and radio buttons drawn by draw_check (tests) */
static HBRUSH g_darkBg, g_darkList;
#define DARK_BG   RGB(0x20, 0x20, 0x20)
#define DARK_LIST RGB(0x2B, 0x2B, 0x2B)
#define DARK_TEXT RGB(0xF2, 0xF2, 0xF2)
#define DARK_DIM  RGB(0x8A, 0x8A, 0x8A)
#define DARK_LINE RGB(0x5A, 0x5A, 0x5A)
static int g_line, g_statusH;  /* layout: one text line, status line height (pixels) */
static NOTIFYICONDATAW g_nid; static UINT g_taskbarCreated;
static DWORD g_mode = 0, g_onwake = 0, g_lang = L_EN, g_respect = 1;
/* g_lastSet: the level Glim set or accepted (-1 = not synced with the driver yet);
   g_lastSeen: the level seen by the last poll; g_override: level chosen by the user via Fn+Space */
static int g_lastSet = -1, g_override = -1, g_lastSeen = -1;
static ULONGLONG g_graceUntil = 0;  /* changes during this window are treated as system resets, not user actions */
static int g_graceRetries = 0;      /* grace extensions after a failed revert, see T_POLL */
static int g_display = -1;          /* last console display state: 0 off, 1 on, 2 dimmed, -1 not known yet */
static int g_startup = 0;           /* launched by autostart (/startup) */
static int g_tookOver = 0;          /* replaced an older running copy */
static int g_trayRetries = 0;       /* failed attempts to add the tray icon */
static int g_updBusy = 0;           /* an update check is running */
static int g_scale = 1;  /* test builds: GLIM_TIMESCALE speeds up timers */
static void start_grace(UINT ms_) { ULONGLONG u = GetTickCount64() + ms_; if (u > g_graceUntil) g_graceUntil = u; }
static void begin_grace(UINT ms_) { g_graceRetries = 0; start_grace(ms_); }
static int eff_mode(void) { return (g_respect && g_override >= 0) ? g_override : (int)g_mode; }
static UINT ms(UINT v) { return v / (UINT)g_scale ? v / (UINT)g_scale : 1; }
static wchar_t g_dir[MAX_PATH];
static DWORD g_lastErr = 0;
static int g_quiet = 0, g_failNotified = 0;
static int g_startRetrying = 0;  /* sign-in: the driver may not be ready yet - a failure is shown only if the last retry fails */

#define T(id) STR[g_lang][id]
static const GUID MY_GUID_CONSOLE_DISPLAY_STATE = {0x6fe69556,0x704a,0x47a0,{0x8f,0x24,0xc2,0x8d,0x93,0x6f,0xda,0x47}};
static int S(int v) { return MulDiv(v, g_dpi, 96); }

/* ---------- log ---------- */
static void init_dir(void) {
    wchar_t *p = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_LocalAppData, 0, NULL, &p))) swprintf(g_dir, MAX_PATH, L"%ls\\Glim", p);
    else wcscpy(g_dir, L".");
    CoTaskMemFree(p);  /* also after a failure, per the docs; NULL is fine */
    CreateDirectoryW(g_dir, NULL);
}
static void klog(const wchar_t *fmt, ...) {
    wchar_t path[MAX_PATH], line[512], msg[400]; SYSTEMTIME t; GetLocalTime(&t);
    va_list ap; va_start(ap, fmt); vswprintf(msg, 400, fmt, ap); va_end(ap); msg[399] = 0;  /* cut, never unterminated */
    swprintf(path, MAX_PATH, L"%ls\\log.txt", g_dir);
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &fa) && fa.nFileSizeLow > 256 * 1024) {
        wchar_t old[MAX_PATH]; swprintf(old, MAX_PATH, L"%ls\\log.old.txt", g_dir); MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
    }
    swprintf(line, 512, L"%04d-%02d-%02d %02d:%02d:%02d  %ls\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, msg);
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        char u8[2048]; int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, u8, sizeof u8, NULL, NULL);  /* UTF-8: up to 3 bytes per UTF-16 unit */
        DWORD w; if (n > 1) WriteFile(f, u8, (DWORD)(n - 1), &w, NULL); CloseHandle(f);
    }
}

/* ---------- driver ----------
   Two Lenovo drivers answer without admin rights; both use 4-byte input and output buffers.
   - \\.\EnergyDrv (IdeaPad, Xiaoxin, ThinkBook; found by gonwan.com): IOCTL 0x83102144, the input is a function.
     0x01 capability: bit 0 = valid, >> 1 = the highest mode (3: with Auto). 0x32 status: bit 0 = valid, >> 1 =
     the mode in the low bits, and 0x8000 there on models with Auto (without it the keyboard is off or has no
     backlight); older models without Auto (ThinkBook 14 G2) don't set 0x8000.
     Set: 0x00033 / 0x10033 / 0x20033 / 0x30033 = Off / Low / High / Auto.
   - \\.\IBMPmDrv (ThinkPad; Lenovo Vantage's keyboard add-in, "multiple light control"): 0x222680 get (input 0):
     bit 31 = error, bits 16 and 18 = supported, bits 8-11 = the highest mode, bits 0-3 = the mode. 0x222684 set:
     the input keeps the current value's flag bits as Lenovo builds it - ((cur >> 13) & 0x100) | (cur & 0xF0) | mode.
     No Auto. */
#define DRV_ENERGY 1
#define DRV_PM     2
#define IOCTL_PM_GET 0x222680
#define IOCTL_PM_SET 0x222684
static const wchar_t *const DRV_DEV[3]  = { L"", L"\\\\.\\EnergyDrv", L"\\\\.\\IBMPmDrv" };
static const wchar_t *const DRV_NAME[3] = { L"", L"EnergyDrv", L"IBMPmDrv (ThinkPad)" };
static int g_drv = 0;       /* the driver that answered last (0: none yet) */
static int g_maxLevel = 3;  /* the highest mode this keyboard has: 3 = up to Auto, 2 = up to High, 1 = Off/Low */
static wchar_t g_raw[64]; static int g_rawDrv;  /* the last raw answer and its driver, for the log and the self-test */

/* test hook: GLIM_FAKE=<file> emulates a driver with a file holding the level digit (no file = no driver);
   GLIM_FAKE_DRV picks it: energy (default), energy-old (no 0x8000, no Auto), energy-nocap (no capability answer,
   no 0x8000), energy-nobit (Auto but no 0x8000: no backlight), energy-dead (sets change nothing), pm (ThinkPad),
   pm1 (Off/Low only), pm0 (no backlight), pm-fail (refuses every set), pm-flags (more flag bits), pm-err (error
   bit), pm-half (one of the two "supported" bits), both (EnergyDrv without backlight + IBMPmDrv) */
static wchar_t g_fake[MAX_PATH], g_fakeDrv[16];
#define FAKE_HANDLE(d) ((HANDLE)(INT_PTR)(0x7100 + (d)))
static int fake_is(const wchar_t *v) { return !wcscmp(g_fakeDrv, v); }
static int fake_has(int drv) { return fake_is(L"both") || (drv == DRV_PM) == !wcsncmp(g_fakeDrv, L"pm", 2); }
static int fake_read(void) {
    HANDLE f = CreateFileW(g_fake, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return -1;
    char c = 0; DWORD rd = 0; ReadFile(f, &c, 1, &rd, NULL); CloseHandle(f);
    return (rd == 1 && c >= '0' && c <= '9') ? c - '0' : 15;
}
static int fake_write(int level) {
    HANDLE f = CreateFileW(g_fake, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, TRUNCATE_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    char c = (char)('0' + level); DWORD w; WriteFile(f, &c, 1, &w, NULL); CloseHandle(f); return 1;
}
static int fake_ioctl(HANDLE h, DWORD code, DWORD in, DWORD *out) {
    int lv = fake_read(), old = fake_is(L"energy-old") || fake_is(L"energy-nocap"), noauto = old || fake_is(L"energy-dead");
    *out = 0;
    if (lv < 0) { SetLastError(ERROR_DEVICE_NOT_CONNECTED); return 0; }
    if (h == FAKE_HANDLE(DRV_ENERGY) && code == IOCTL_BACKLIGHT) {
        static const DWORD f[4] = { 0x00033, 0x10033, 0x20033, 0x30033 };
        if (in == 0x01) {
            if (fake_is(L"energy-nocap")) { SetLastError(ERROR_INVALID_FUNCTION); return 0; }
            *out = fake_is(L"both") ? 1 : noauto ? (2 << 1) | 1 : (3 << 1) | 1; return 1;
        }
        if (in == 0x32) { *out = ((old || fake_is(L"energy-nobit") ? 0 : 0x8000) | (DWORD)lv) << 1 | 1; return 1; }
        for (int i = 0; i <= (noauto ? 2 : 3); i++) if (in == f[i]) return fake_is(L"energy-dead") ? 1 : fake_write(i);
    } else if (h == FAKE_HANDLE(DRV_PM)) {
        DWORD maxl = fake_is(L"pm1") ? 1 : 2, flags = 0x50000 | maxl << 8;
        if (fake_is(L"pm0")) flags = 0;
        if (fake_is(L"pm-flags")) flags = 0x250230;   /* bit 21 and bits 4-7 set: a set must carry 0x130 */
        if (fake_is(L"pm-err")) flags |= 0x80000000;
        if (fake_is(L"pm-half")) flags = 0x10000 | maxl << 8;
        DWORD keep = ((flags >> 13) & 0x100) | (flags & 0xF0);  /* what Lenovo's composition carries over */
        if (code == IOCTL_PM_GET && in == 0) { *out = flags ? flags | (DWORD)lv : 0; return 1; }
        if (code == IOCTL_PM_SET) {
            if ((in & ~0xFu) != keep || (in & 0xF) > maxl || (flags & 0x50000) != 0x50000 || fake_is(L"pm-fail")) { *out = 0x80000000; return 1; }
            return fake_write((int)(in & 0xF));
        }
    }
    SetLastError(ERROR_INVALID_FUNCTION); return 0;
}

static HANDLE dev_open(int drv) {
    if (g_fake[0]) {
        if (!fake_has(drv) || GetFileAttributesW(g_fake) == INVALID_FILE_ATTRIBUTES) {
            SetLastError(ERROR_FILE_NOT_FOUND); return INVALID_HANDLE_VALUE;
        }
        return FAKE_HANDLE(drv);
    }
    return CreateFileW(DRV_DEV[drv], GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
}
static void dev_close(HANDLE h) { if (!g_fake[0]) CloseHandle(h); }
static int dev_ioctl(HANDLE h, DWORD code, DWORD in, DWORD *out) {  /* 1 = answered (*out); 0 = failed (g_lastErr) */
    unsigned char b[8] = {0}; DWORD len = 0;
    if (g_fake[0]) { if (fake_ioctl(h, code, in, out)) return 1; g_lastErr = GetLastError(); return 0; }
    if (!DeviceIoControl(h, code, &in, sizeof in, b, 4, &len, NULL)) { g_lastErr = GetLastError(); return 0; }
    *out = b[0] | (b[1] << 8) | (b[2] << 16) | ((DWORD)b[3] << 24); return 1;
}

/* an answer Glim doesn't understand: logged once per value */
static int unknown_answer(int drv) {
    static wchar_t logged[3][64];
    if (wcscmp(logged[drv], g_raw) != 0) { wcscpy(logged[drv], g_raw); klog(L"driver: %ls answered %ls - not a known backlight status", DRV_NAME[drv], g_raw); }
    g_lastErr = ERROR_NOT_SUPPORTED; return -2;
}
/* the backends: 0..3 = the mode now (or the one just set), -2 = no backlight / unknown answer, -4 = the
   keyboard doesn't have the mode asked for, -5 = the driver didn't take the change; *maxl = its highest mode */
static int energy_backlight(HANDLE h, int set_level, int *maxl) {
    DWORD cap = 0, st = 0, d = 0, capErr, stErr;
    int capOk = dev_ioctl(h, IOCTL_BACKLIGHT, 0x01, &cap); capErr = g_lastErr;
    int stOk = dev_ioctl(h, IOCTL_BACKLIGHT, 0x32, &st); stErr = g_lastErr;
    wchar_t c[32], t[32];  /* both answers (or errors) for the log and the self-test */
    if (capOk) swprintf(c, 32, L"capability 0x%08lX", cap); else swprintf(c, 32, L"capability failed (%lu)", capErr);
    if (stOk) swprintf(t, 32, L"status 0x%08lX", st); else swprintf(t, 32, L"status failed (%lu)", stErr);
    swprintf(g_raw, 64, L"%ls, %ls", c, t); g_rawDrv = DRV_ENERGY;
    if (!stOk) { g_lastErr = stErr; return -2; }
    int lv = (int)((st >> 1) & 0x7fff), top = capOk && (cap & 1) && (cap >> 1) <= 3 ? (int)(cap >> 1) : -1;  /* -1: not known */
    if (!(st & 1) || lv > 3 || top == 0) return unknown_answer(DRV_ENERGY);
    if (top == 3 && !(st & 0x10000)) return unknown_answer(DRV_ENERGY);  /* models with Auto set 0x8000; without it: no backlight */
    if (top < 0) top = lv == 3 ? 3 : 2;  /* no capability answer (older models): no Auto unless it shows Auto */
    if (lv > top) top = lv;  /* it shows the mode: it has it */
    *maxl = top;
    if (set_level < 0) return lv;
    if (set_level > top) return -4;
    static const DWORD f[4] = { 0x00033, 0x10033, 0x20033, 0x30033 };
    return dev_ioctl(h, IOCTL_BACKLIGHT, f[set_level], &d) ? set_level : -5;
}
static int pm_backlight(HANDLE h, int set_level, int *maxl) {
    DWORD st = 0, d = 0;
    if (!dev_ioctl(h, IOCTL_PM_GET, 0, &st)) return -2;
    swprintf(g_raw, 64, L"status 0x%08lX", st); g_rawDrv = DRV_PM;
    int lv = (int)(st & 0xF), top = (int)((st >> 8) & 0xF);
    if ((st & 0x80000000) || (st & 0x50000) != 0x50000 || top < 1) return unknown_answer(DRV_PM);
    if (top > 2) top = 2;  /* this driver has no Auto */
    if (lv > top) return unknown_answer(DRV_PM);
    *maxl = top;
    if (set_level < 0) return lv;
    if (set_level > top) return -4;
    DWORD v = ((st >> 13) & 0x100) | (st & 0xF0) | (DWORD)set_level;
    if (!dev_ioctl(h, IOCTL_PM_SET, v, &d)) return -5;
    if (d & 0x80000000) { g_lastErr = ERROR_GEN_FAILURE; return -5; }  /* the driver refused it */
    return set_level;
}
/* returns: 0..3 the mode now (or the one just set); -1 no driver; -3 access denied; -2 no backlight control or an
   unknown answer; -4 this keyboard doesn't have the mode asked for; -5 the driver didn't take the change.
   set_level < 0 only reads. The driver that answered last is tried first, and once one has worked, a failed call
   doesn't switch to the other. */
static int backlight(int set_level) {
    int res = -1; DWORD err = ERROR_FILE_NOT_FOUND;
    g_raw[0] = 0;
    for (int i = 0; i < 2; i++) {
        int d = (g_drv == DRV_PM) == (i == 0) ? DRV_PM : DRV_ENERGY, maxl = 3;
        HANDLE h = dev_open(d);
        if (h == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            if (e == ERROR_ACCESS_DENIED) { res = -3; err = e; }  /* the one the user can do something about */
            else if (res == -1 && e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) err = e;
            continue;
        }
        g_lastErr = 0;
        int r = d == DRV_ENERGY ? energy_backlight(h, set_level, &maxl) : pm_backlight(h, set_level, &maxl);
        dev_close(h);
        if (r >= 0 || r <= -4) {  /* it's the driver of this keyboard */
            if (g_drv != d || g_maxLevel != maxl)
                klog(L"driver: %ls, %ls, modes off..%ls", DRV_NAME[d], g_raw, LOG_MODE[maxl]);
            g_drv = d; g_maxLevel = maxl;
            if (r == -4) g_lastErr = ERROR_NOT_SUPPORTED;
            return r;
        }
        if (res != -3) { res = -2; err = g_lastErr; }  /* it's there, but no backlight: say that rather than "missing" */
        if (d == g_drv) break;
    }
    g_lastErr = err; return res;
}
static const wchar_t *err_text(int r) {
    return r == -1 ? T(S_E_MISSING) : r == -3 ? T(S_E_DENIED) : r == -4 ? T(S_E_NOMODE) : r == -5 ? T(S_E_REFUSED) : T(S_E_UNSUP);
}

/* ---------- settings ---------- */
static int reg_dword(HKEY k, const wchar_t *name, DWORD *v) {
    DWORD type, sz = sizeof *v;
    return RegQueryValueExW(k, name, NULL, &type, (BYTE*)v, &sz) == ERROR_SUCCESS && type == REG_DWORD && sz == sizeof *v;
}
static void load_settings(void) {
    HKEY k; DWORD v;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (reg_dword(k, L"Mode", &v) && v <= 3) g_mode = v;
        if (reg_dword(k, L"OnWake", &v)) g_onwake = v ? 1 : 0;
        if (reg_dword(k, L"Lang", &v) && v < L_COUNT) g_lang = v;
        if (reg_dword(k, L"RespectManual", &v)) g_respect = v ? 1 : 0;
        RegCloseKey(k);
    }
}
static void save_settings(void) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) == ERROR_SUCCESS) {
        RegSetValueExW(k, L"Mode", 0, REG_DWORD, (BYTE*)&g_mode, sizeof g_mode);
        RegSetValueExW(k, L"OnWake", 0, REG_DWORD, (BYTE*)&g_onwake, sizeof g_onwake);
        RegSetValueExW(k, L"Lang", 0, REG_DWORD, (BYTE*)&g_lang, sizeof g_lang);
        RegSetValueExW(k, L"RespectManual", 0, REG_DWORD, (BYTE*)&g_respect, sizeof g_respect);
        RegCloseKey(k);
    }
}

/* ---------- autostart: the Run value "Glim" ---------- */
static int read_run(wchar_t *out, DWORD cch) {
    HKEY k; int r = 0; DWORD sz = (cch - 1) * sizeof(wchar_t), type;  /* room for a terminator the value may lack */
    out[0] = 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (RegQueryValueExW(k, RUN_NAME, NULL, &type, (BYTE*)out, &sz) == ERROR_SUCCESS && type == REG_SZ) { out[sz / sizeof(wchar_t)] = 0; r = 1; }
        RegCloseKey(k);
    }
    return r;
}
static int run_target(wchar_t *path) {
    wchar_t v[MAX_PATH + 32]; path[0] = 0;
    if (!read_run(v, MAX_PATH + 32)) return 0;
    const wchar_t *a = wcschr(v, L'"'), *b = a ? wcschr(a + 1, L'"') : NULL;
    if (a && b && b - a - 1 < MAX_PATH) { wcsncpy(path, a + 1, (size_t)(b - a - 1)); path[b - a - 1] = 0; }
    return 1;
}
static int autostart_enabled(void) { wchar_t p[MAX_PATH]; return run_target(p); }
static void write_run(const wchar_t *exe) {
    HKEY k; wchar_t cmd[MAX_PATH + 32];
    wchar_t cur[MAX_PATH + 32];
    swprintf(cmd, MAX_PATH + 32, L"\"%ls\" /startup", exe);
    if (read_run(cur, MAX_PATH + 32) && !wcscmp(cur, cmd)) return;  /* unchanged: Apply doesn't register autostart again */
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_WRITE, NULL, &k, NULL) != ERROR_SUCCESS) return;
    RegSetValueExW(k, RUN_NAME, 0, REG_SZ, (BYTE*)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}
static void self_path(wchar_t *p) { GetModuleFileNameW(NULL, p, MAX_PATH); }
static int file_exists(const wchar_t *p) { return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES; }
/* FILEVERSION of an exe as one number (see APP_FILEVER); 0 = missing or no version info */
static ULONGLONG file_version(const wchar_t *path) {
    if (!g_haveVersion) return 0;
    DWORD h = 0, n = p_GetFileVersionInfoSizeW(path, &h); ULONGLONG v = 0;
    void *buf = n ? HeapAlloc(GetProcessHeap(), 0, n) : NULL;
    if (!buf) return 0;
    VS_FIXEDFILEINFO *fi = NULL; UINT len = 0;
    if (p_GetFileVersionInfoW(path, 0, n, buf) && p_VerQueryValueW(buf, L"\\", (void**)&fi, &len) && fi && len >= sizeof *fi)
        v = ((ULONGLONG)fi->dwFileVersionMS << 32) | fi->dwFileVersionLS;
    HeapFree(GetProcessHeap(), 0, buf);
    return v;
}
/* ---------- autostart: the Run value starts this Glim.exe from where it is ----------
   Nothing is copied: a program that copies itself elsewhere and registers the copy for autostart looks like
   malware to antivirus behaviour monitors. The user keeps Glim.exe in a permanent folder. The Run value is
   written only on the user's request (Apply, /autostart on). */
/* path is inside dir: both in their long form (TEMP is often 8.3: C:\\Users\\ADMINI~1\\...), case-insensitive in Unicode */
static int under_dir(const wchar_t *path, const wchar_t *dir) {
    wchar_t lp[MAX_PATH], ld[MAX_PATH]; DWORD n;
    n = GetLongPathNameW(path, lp, MAX_PATH);
    if (!n || n >= MAX_PATH) { wcsncpy(lp, path, MAX_PATH - 1); lp[MAX_PATH - 1] = 0; }
    n = GetLongPathNameW(dir, ld, MAX_PATH);
    if (!n || n >= MAX_PATH) { wcsncpy(ld, dir, MAX_PATH - 1); ld[MAX_PATH - 1] = 0; }
    n = (DWORD)wcslen(ld);
    while (n > 3 && ld[n - 1] == L'\\') ld[--n] = 0;  /* "C:\\Temp\\" -> "C:\\Temp" */
    return n && wcslen(lp) > n && lp[n] == L'\\' && CompareStringOrdinal(lp, (int)n, ld, (int)n, TRUE) == CSTR_EQUAL;
}
static int in_temp_folder(const wchar_t *path) {  /* Downloads or %TEMP%: files there get cleaned up */
    wchar_t *dl = NULL, tmp[MAX_PATH]; int r = 0; DWORD n;
    if (SUCCEEDED(SHGetKnownFolderPath(&FOLDERID_Downloads, 0, NULL, &dl)) && dl) r = under_dir(path, dl);
    CoTaskMemFree(dl);
    if (!r && (n = GetTempPathW(MAX_PATH, tmp)) != 0 && n < MAX_PATH) r = under_dir(path, tmp);
    return r;
}
/* the autostart target is really gone - not a drive that isn't there, a locked file or no access */
static int target_gone(const wchar_t *p) {
    if (GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) return 0;
    DWORD e = GetLastError();
    if (e != ERROR_FILE_NOT_FOUND && e != ERROR_PATH_NOT_FOUND) return 0;
    if (p[0] && p[1] == L':' && p[2] == L'\\') {  /* a drive letter: the drive itself must be there (a USB stick out = not gone) */
        wchar_t root[4] = { p[0], L':', L'\\', 0 };
        if (GetFileAttributesW(root) == INVALID_FILE_ATTRIBUTES) return 0;
    }
    return 1;
}
static void say_box(const wchar_t *text, UINT icon) {  /* test runs (GLIM_FAKE): to the log */
    if (g_fake[0]) klog(L"says: %ls", text);
    else MessageBoxW(g_wnd && IsWindowVisible(g_wnd) ? g_wnd : NULL, text, L"Glim", MB_OK | icon);
}
static void set_autostart(int on) {
    wchar_t self[MAX_PATH], p[MAX_PATH];
    if (!on) {
        HKEY k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_WRITE, &k) == ERROR_SUCCESS) {
            if (RegDeleteValueW(k, RUN_NAME) == ERROR_SUCCESS) klog(L"autostart: off");
            RegCloseKey(k);
        }
        return;
    }
    self_path(self);
    /* an older exe doesn't take autostart away from a newer Glim that is still there */
    if (run_target(p) && p[0] && _wcsicmp(p, self) != 0 && file_exists(p) && file_version(p) > APP_FILEVER) {
        klog(L"autostart: kept %ls, it is newer than %ls", p, APP_VERSION);
        return;
    }
    if (!file_exists(self)) { klog(L"autostart: not set, %ls has been moved", self); return; }  /* moved while running */
    if (in_temp_folder(self)) {  /* refused: it would break when the folder is cleaned (and antiviruses dislike autostart there) */
        wchar_t dir[MAX_PATH], t[800], *slash;
        wcscpy(dir, self); if ((slash = wcsrchr(dir, L'\\')) != NULL) *slash = 0;
        klog(L"autostart: not set, %ls is in a folder for temporary files", self);
        swprintf(t, 800, T(S_AUTO_TEMP), dir);
        if (g_wnd && !g_startup) say_box(t, MB_ICONWARNING);  /* not for /autostart on, not at sign-in */
        return;
    }
    write_run(self);
    klog(L"autostart: on -> %ls", self);
}
/* Apply with the box ticked makes autostart start this exe when autostart is off or its target is gone, or when
   the target is an older Glim - not because another copy of the same version was opened to change the mode;
   never from a newer Glim; not for an exe set_autostart refuses */
static int autostart_would_switch(void) {
    wchar_t p[MAX_PATH], self[MAX_PATH]; ULONGLONG v;
    self_path(self);
    if (!file_exists(self) || in_temp_folder(self)) return 0;
    if (!run_target(p) || !p[0] || target_gone(p)) return 1;
    if (!_wcsicmp(p, self)) return 0;
    v = file_version(p);
    return v && v < APP_FILEVER;
}
static int g_autoSwitch;  /* autostart is on, and Apply would move it to this exe (cached: it reads files) */
static void autostart_refresh(void) { wchar_t p[MAX_PATH]; g_autoSwitch = run_target(p) && autostart_would_switch(); }
static void autostart_apply(int want) {
    wchar_t p[MAX_PATH];
    int have = run_target(p);
    if (!want) { if (have) set_autostart(0); return; }
    if (!have || autostart_would_switch()) set_autostart(1);  /* off -> on: set_autostart says why if it refuses */
}
static void balloon_icon(const wchar_t *text, DWORD icon);
/* on launch. Autostart isn't changed behind the user's back: a missing target turns it off (an antivirus may
   have removed it - not put back), an older Glim in it is only reported (Apply switches it to this exe) */
static void heal_autostart(void) {
    wchar_t p[MAX_PATH], self[MAX_PATH]; ULONGLONG v;
    self_path(self);
    if (run_target(p) && p[0]) {  /* a value without quotes isn't Glim's: left alone */
        if (target_gone(p)) {
            klog(L"autostart target missing (%ls) - autostart turned off", p);
            set_autostart(0);
            balloon_icon(T(S_AUTO_GONE), NIIF_WARNING);
        } else if (_wcsicmp(p, self) != 0 && (v = file_version(p)) != 0 && v < APP_FILEVER) {
            klog(L"autostart starts an older Glim (%ls) - Apply switches it to this one", p);
            balloon_icon(T(S_AUTO_OLDER), NIIF_INFO);
        }
    }
    autostart_refresh();
}


/* ---------- UI helpers ---------- */
static BOOL tray_send(DWORD msg) {  /* Shell_NotifyIcon, remembering what the taskbar accepted */
    if (!Shell_NotifyIconW(msg, &g_nid)) return FALSE;
    if (g_nid.uFlags & NIF_ICON) g_trayShown = g_nid.hIcon;
    if (g_nid.uFlags & NIF_TIP) wcscpy(g_trayTipShown, g_nid.szTip);
    return TRUE;
}
static HICON tray_icon(void) { return g_trayLevel >= 0 && g_trayIcon[g_trayLevel] ? g_trayIcon[g_trayLevel] : g_smallIcon; }
static void tray_tip(int cur) {  /* the tooltip and the icon show the level the driver reports */
    wchar_t tip[128];
    g_trayLevel = (cur >= 0 && cur <= 3) ? cur : -1;
    swprintf(tip, 128, T(S_TIP), g_trayLevel >= 0 ? STR[g_lang][S_MODE0 + cur] : L"?");
    if (tray_icon() == g_trayShown && !wcscmp(tip, g_trayTipShown)) return;  /* polls in a grace window repeat it */
    wcscpy(g_nid.szTip, tip); g_nid.hIcon = tray_icon();
    g_nid.uFlags = NIF_TIP | NIF_ICON;
    tray_send(NIM_MODIFY);
}
/* system metrics for the window's DPI (per-monitor), not the primary monitor's */
static int sysmet(int idx) {
    return p_GetSystemMetricsForDpi ? p_GetSystemMetricsForDpi(idx, (UINT)g_dpi) : GetSystemMetrics(idx);
}
/* height of wrapped text in the current font, as BUTTON/STATIC controls draw it */
static int text_height_in(HFONT f, const wchar_t *s, int w) {
    HDC dc = GetDC(g_wnd); HGDIOBJ old = SelectObject(dc, f);
    RECT r = { 0, 0, w, 0 };
    DrawTextW(dc, s, -1, &r, DT_CALCRECT | DT_WORDBREAK | DT_EXPANDTABS);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    return r.bottom - r.top;
}
static int text_height(const wchar_t *s, int w) { return text_height_in(g_font, s, w); }
static int text_width(const wchar_t *s) {
    HDC dc = GetDC(g_wnd); HGDIOBJ old = SelectObject(dc, g_font); SIZE sz = { 0, 0 };
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old); ReleaseDC(g_wnd, dc);
    return sz.cx;
}
static void place(int id, int x, int y, int w, int h) { SetWindowPos(GetDlgItem(g_wnd, id), NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE); }
static int status_height(void) {  /* at least two lines, so most messages don't resize the window */
    wchar_t s[256] = L""; GetDlgItemTextW(g_wnd, ID_STATUS, s, 256);
    int h = text_height(s, S(324));
    return h > 2 * g_line ? h : 2 * g_line;
}
static int check_box_w(void) { return sysmet(SM_CXMENUCHECK) + S(8); }  /* room for a check box glyph and the gap after it */
/* places every control for the current DPI and font; sizes the checkboxes and the status line to their texts
   (they differ by language and font) and moves the rest below them */
static void layout(void) {
    static const int checks[3] = { ID_AUTOSTART, ID_ONWAKE, ID_RESPECT };
    int x = S(16), w = S(324), box = check_box_w(), y = S(146);
    place(ID_GROUP, S(12), S(8), S(316), S(128));
    for (int i = 0; i < 4; i++) place(ID_R0 + i, S(28), S(30 + i * 25), S(280), S(22));
    g_line = text_height(L"Ag", 1000);
    for (int i = 0; i < 3; i++) {
        wchar_t t[256] = L""; GetDlgItemTextW(g_wnd, checks[i], t, 256);
        int h = text_height_in(g_fontBold ? g_fontBold : g_font, t, w - box) + S(6); if (h < S(22)) h = S(22);  /* room for bold */
        place(checks[i], x, y, w, h); y += h + S(4);
    }
    y += S(2);
    g_statusH = status_height(); place(ID_STATUS, x, y, w, g_statusH); y += g_statusH + S(6);
    place(ID_LANGLBL, x, y + S(4), S(90), S(24));
    place(ID_LANG, S(108), y + S(2), S(116), S(200));
    place(ID_ABOUT, S(232), y, S(96), S(28));
    y += S(34);
    {
        wchar_t t[128] = L""; GetDlgItemTextW(g_wnd, ID_UPDATE, t, 128);
        int uw = text_width(t) + S(24), un = text_width(T(S_UPD_CHECKING)) + S(24);
        if (uw < un) uw = un;
        if (uw < S(96)) uw = S(96);
        if (uw > S(316)) uw = S(316);
        place(ID_UPDATE, S(328) - uw, y, uw, S(28));
    }
    y += S(42);
    place(ID_TEST, S(12), y, S(100), S(30));
    place(ID_APPLY, S(128), y, S(96), S(30));
    place(ID_HIDE, S(232), y, S(96), S(30));
    RECT r = { 0, 0, S(344), y + S(42) };
    DWORD st = (DWORD)GetWindowLongPtrW(g_wnd, GWL_STYLE), ex = (DWORD)GetWindowLongPtrW(g_wnd, GWL_EXSTYLE);
    if (!p_AdjustWindowRectExForDpi || !p_AdjustWindowRectExForDpi(&r, st, FALSE, ex, (UINT)g_dpi)) AdjustWindowRectEx(&r, st, FALSE, ex);
    SetWindowPos(g_wnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    InvalidateRect(g_wnd, NULL, TRUE);
}
/* Segoe UI has no CJK glyphs and Windows mixes fallback fonts for them, so Chinese gets its own font */
static int CALLBACK font_found(const LOGFONTW *lf, const TEXTMETRICW *tm, DWORD type, LPARAM found) {
    (void)lf; (void)tm; (void)type; *(int*)found = 1; return 0;
}
static HFONT make_font(const LOGFONTW *base, const wchar_t *face) {  /* NULL if the face isn't installed */
    LOGFONTW lf = *base; wcsncpy(lf.lfFaceName, face, LF_FACESIZE - 1); lf.lfFaceName[LF_FACESIZE - 1] = 0;
    LOGFONTW q = { 0 }; q.lfCharSet = DEFAULT_CHARSET; wcscpy(q.lfFaceName, lf.lfFaceName);
    int found = 0; HDC dc = GetDC(NULL);
    EnumFontFamiliesExW(dc, &q, font_found, (LPARAM)&found, 0);
    ReleaseDC(NULL, dc);
    return found ? CreateFontIndirectW(&lf) : NULL;
}
static void set_font(void) {
    g_font = (g_lang == L_ZH && g_fontZh) ? g_fontZh : g_fontUi;
    g_fontBold = (g_lang == L_ZH && g_fontZhB) ? g_fontZhB : g_fontUiB;
    for (HWND c = GetWindow(g_wnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
}
static int g_sysDpi = 96;  /* the DPI SystemParametersInfo reports in */
/* the system message font (and the Chinese one) for the window's DPI; replaces the fonts of the controls */
static void make_fonts(void) {
    NONCLIENTMETRICSW ncm; ZeroMemory(&ncm, sizeof ncm); ncm.cbSize = sizeof ncm;
    if (!p_SystemParametersInfoForDpi || !p_SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, (UINT)g_dpi)) {
        ZeroMemory(&ncm, sizeof ncm); ncm.cbSize = sizeof ncm;
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
        ncm.lfMessageFont.lfHeight = MulDiv(ncm.lfMessageFont.lfHeight, g_dpi, g_sysDpi);
    }
    HFONT oldUi = g_fontUi, oldZh = g_fontZh, oldUiB = g_fontUiB, oldZhB = g_fontZhB;
    LOGFONTW bold = ncm.lfMessageFont; bold.lfWeight = FW_BOLD;
    g_fontUi = CreateFontIndirectW(&ncm.lfMessageFont);
    g_fontUiB = CreateFontIndirectW(&bold);
    g_fontZh = make_font(&ncm.lfMessageFont, L"Microsoft YaHei UI");
    g_fontZhB = make_font(&bold, L"Microsoft YaHei UI");
    if (!g_fontZh) { g_fontZh = make_font(&ncm.lfMessageFont, L"Microsoft YaHei"); if (g_fontZhB) DeleteObject(g_fontZhB); g_fontZhB = make_font(&bold, L"Microsoft YaHei"); }
    set_font();  /* the controls stop using the old fonts before they are deleted */
    if (oldUi) DeleteObject(oldUi);
    if (oldZh) DeleteObject(oldZh);
    if (oldUiB) DeleteObject(oldUiB);
    if (oldZhB) DeleteObject(oldZhB);
}
/* title bar and Alt+Tab icons drawn for the window's DPI (the class icons are for the primary monitor) */
static void set_window_icons(void) {
    static HICON big, small_;
    HICON b = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, sysmet(SM_CXICON), sysmet(SM_CYICON), LR_DEFAULTCOLOR);
    HICON s = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, sysmet(SM_CXSMICON), sysmet(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (b) { SendMessageW(g_wnd, WM_SETICON, ICON_BIG, (LPARAM)b); if (big) DestroyIcon(big); big = b; }
    if (s) { SendMessageW(g_wnd, WM_SETICON, ICON_SMALL, (LPARAM)s); if (small_) DestroyIcon(small_); small_ = s; }
}

/* ---------- dark mode: follows "Choose your app mode" in Windows settings (never in high contrast) ----------
   Classic controls have no dark look of their own: the title bar asks DWM, push buttons and the combo box take
   Windows' own dark themes (DarkMode_Explorer, DarkMode_CFD; where they're missing they stay light and readable),
   check boxes, radio buttons and the group box are drawn here, the rest takes colours from WM_CTLCOLOR*. */
static int dark_wanted(void) {
    HIGHCONTRASTW hc; HKEY k; DWORD v = 1;
    if (g_build < 17763 || !g_haveTheme) return 0;  /* Windows 10 1809 brought the dark themes */
    ZeroMemory(&hc, sizeof hc); hc.cbSize = sizeof hc;
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof hc, &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON)) return 0;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", 0, KEY_READ, &k) == ERROR_SUCCESS) {
        if (!reg_dword(k, L"AppsUseLightTheme", &v)) v = 1;
        RegCloseKey(k);
    }
    return v == 0;
}
static void load_dark_menu_fns(void) {  /* undocumented uxtheme functions: looked up only once dark mode is wanted */
    static int done;
    if (done || !g_uxtheme || g_build < 18362) return;
    done = 1;
    LOAD_ANY(g_uxtheme, p_AllowDarkModeForWindow, MAKEINTRESOURCEA(133));
    LOAD_ANY(g_uxtheme, p_SetPreferredAppMode, MAKEINTRESOURCEA(135));
    LOAD_ANY(g_uxtheme, p_FlushMenuThemes, MAKEINTRESOURCEA(136));
}
static void apply_theme(void) {  /* after g_dark changed (or at start, if dark) */
    if (g_dark) load_dark_menu_fns();
    /* force dark / default (light): Glim decides itself, uxtheme's cached "apps use dark" may lag behind a live switch */
    if (p_SetPreferredAppMode) p_SetPreferredAppMode(g_dark ? 2 : 0);
    if (p_AllowDarkModeForWindow) p_AllowDarkModeForWindow(g_wnd, g_dark);
    if (p_FlushMenuThemes) p_FlushMenuThemes();
    if (p_DwmSetWindowAttribute) {
        BOOL on = g_dark;  /* DWMWA_USE_IMMERSIVE_DARK_MODE: 20, before Windows 10 20H1 19 */
        if (FAILED(p_DwmSetWindowAttribute(g_wnd, 20, &on, sizeof on))) p_DwmSetWindowAttribute(g_wnd, 19, &on, sizeof on);
        if (IsWindowVisible(g_wnd))  /* Windows 10 repaints the title bar only when the frame changes */
            SetWindowPos(g_wnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    for (HWND c = GetWindow(g_wnd, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) {
        wchar_t cls[16] = L""; GetClassNameW(c, cls, 16);
        int combo = !_wcsicmp(cls, L"ComboBox");
        if (combo || !_wcsicmp(cls, L"Button"))  /* NULL, NULL = back to the normal theme */
            p_SetWindowTheme(c, g_dark ? (combo ? L"DarkMode_CFD" : L"DarkMode_Explorer") : NULL, NULL);
    }
    RedrawWindow(g_wnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}
enum { G_BP_RADIO = 2, G_BP_CHECK = 3 };  /* BP_RADIOBUTTON, BP_CHECKBOX (vssym32.h); states 1-4 unchecked, 5-8 checked */
/* NM_CUSTOMDRAW of a check box or radio button in dark mode: the themed control ignores the text colour */
static LRESULT draw_check(const NMCUSTOMDRAW *cd) {
    if (cd->dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    HWND c = cd->hdr.hwndFrom; HDC dc = cd->hdc; RECT r = cd->rc, g, tr;
    DWORD type = (DWORD)GetWindowLongPtrW(c, GWL_STYLE) & BS_TYPEMASK;
    int radio = type == BS_AUTORADIOBUTTON, checked = SendMessageW(c, BM_GETCHECK, 0, 0) == BST_CHECKED;
    int dis = (cd->uItemState & CDIS_DISABLED) != 0, down = (cd->uItemState & CDIS_SELECTED) != 0, hot = (cd->uItemState & CDIS_HOT) != 0;
    int part = radio ? G_BP_RADIO : G_BP_CHECK, state = (checked ? 4 : 0) + (dis ? 4 : down ? 3 : hot ? 2 : 1);
    SIZE gs = { S(13), S(13) };
    FillRect(dc, &r, g_darkBg);
    HTHEME th = p_OpenThemeDataForDpi ? p_OpenThemeDataForDpi(c, L"Button", (UINT)g_dpi) : p_OpenThemeData(c, L"Button");
    /* no DC: the size for the theme's DPI (the DC would give the primary monitor's) */
    if (th && FAILED(p_GetThemePartSize(th, NULL, part, state, NULL, TS_DRAW, &gs))) { gs.cx = S(13); gs.cy = S(13); }
    g.left = r.left; g.top = r.top + (r.bottom - r.top - gs.cy) / 2; g.right = g.left + gs.cx; g.bottom = g.top + gs.cy;
    if (th) { p_DrawThemeBackground(th, dc, part, state, &g, NULL); p_CloseThemeData(th); }
    else DrawFrameControl(dc, &g, DFC_BUTTON, (radio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK) | (checked ? DFCS_CHECKED : 0) | (dis ? DFCS_INACTIVE : 0) | (down ? DFCS_PUSHED : 0));
    wchar_t t[256] = L""; GetWindowTextW(c, t, 256);
    int tx = gs.cx + S(5), box = check_box_w();
    if (tx > box) tx = box;  /* layout() sized the control for text this wide */
    HGDIOBJ of = SelectObject(dc, (HFONT)SendMessageW(c, WM_GETFONT, 0, 0));
    tr.left = r.left + tx; tr.right = r.right; tr.top = tr.bottom = 0;
    DrawTextW(dc, t, -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    int h = tr.bottom - tr.top, right = tr.right;
    tr.top = r.top + (r.bottom - r.top - h) / 2; tr.bottom = tr.top + h; tr.right = r.right;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, dis ? DARK_DIM : DARK_TEXT);
    DrawTextW(dc, t, -1, &tr, DT_WORDBREAK | DT_NOPREFIX);
    if ((cd->uItemState & CDIS_FOCUS) && !(SendMessageW(c, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS)) {
        RECT f = { tr.left - 1, tr.top, (right < r.right ? right : r.right) + 1, tr.bottom };
        DrawFocusRect(dc, &f);
    }
    SelectObject(dc, of);
    g_customDraws++;
    return CDRF_SKIPDEFAULT;
}
/* the group box in dark mode: a grey frame and a light caption (subclass; light mode = the normal control) */
static LRESULT CALLBACK group_proc(HWND h, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref) {
    (void)id; (void)ref;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(h, group_proc, 1);
    if (msg != WM_PAINT || !g_dark) return DefSubclassProc(h, msg, wp, lp);
    PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; wchar_t t[128] = L""; SIZE ts = { 0, 0 };
    GetClientRect(h, &r); GetWindowTextW(h, t, 128);
    HGDIOBJ of = SelectObject(dc, (HFONT)SendMessageW(h, WM_GETFONT, 0, 0));
    GetTextExtentPoint32W(dc, t, (int)wcslen(t), &ts);
    HPEN pen = CreatePen(PS_SOLID, 1, DARK_LINE);
    HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, r.left, r.top + ts.cy / 2, r.right, r.bottom);  /* the inside isn't painted: the radio buttons are there */
    SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pen);
    RECT cap = { S(6), 0, S(10) + ts.cx, ts.cy };
    FillRect(dc, &cap, g_darkBg);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, DARK_TEXT);
    TextOutW(dc, S(8), 0, t, (int)wcslen(t));
    SelectObject(dc, of);
    EndPaint(h, &ps);
    return 0;
}
static void mark_changes(void);
static void enable_modes(void);
static int g_shownLevel = -1;  /* the level the status line shows (< 0: an error) */
static void show_status(int cur) {
    wchar_t s[200];
    g_shownLevel = cur;
    if (cur < 0 || cur > 3) swprintf(s, 200, T(S_ERR), err_text(cur));
    else swprintf(s, 200, (g_respect && g_override >= 0 && cur == g_override) ? T(S_CUR_MANUAL) : T(S_CUR), STR[g_lang][S_MODE0 + cur]);
    if (g_wnd && GetDlgItem(g_wnd, ID_STATUS)) {
        wchar_t old[200] = L""; GetDlgItemTextW(g_wnd, ID_STATUS, old, 200);
        if (wcscmp(old, s) != 0) { SetDlgItemTextW(g_wnd, ID_STATUS, s); if (status_height() != g_statusH) layout(); }
        if (IsWindowVisible(g_wnd)) mark_changes();  /* a manual (Fn+Space) mode makes Apply useful */
    }
    tray_tip(cur);
    enable_modes();
}
static void update_status(void) { show_status(backlight(-1)); }
static void enable_modes(void) {  /* modes this keyboard doesn't have are greyed out (no Auto on a ThinkPad) */
    for (int i = 0; g_wnd && i < 4; i++) {
        HWND c = GetDlgItem(g_wnd, ID_R0 + i);
        if (c && !IsWindowEnabled(c) != (i > g_maxLevel)) EnableWindow(c, i <= g_maxLevel);
    }
}
static void balloon_icon(const wchar_t *text, DWORD icon) {
    klog(L"balloon: %ls", text);
    g_nid.uFlags = NIF_INFO; g_nid.dwInfoFlags = icon;
    wcscpy(g_nid.szInfoTitle, L"Glim"); wcsncpy(g_nid.szInfo, text, 255); g_nid.szInfo[255] = 0;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}
static void balloon(const wchar_t *text) { balloon_icon(text, NIIF_WARNING); }
static void apply_saved(const wchar_t *why) {
    int m = eff_mode();
    int r = backlight(m);
    if (r == m) { g_lastSet = g_lastSeen = m; klog(L"%ls: set %ls%ls - OK", why, LOG_MODE[m], m != (int)g_mode ? L" (manual override)" : L""); }
    else {
        klog(L"%ls: FAILED (%d, err=%lu)", why, r, g_lastErr);
        if (!g_failNotified && !g_startRetrying) { wchar_t t[300]; swprintf(t, 300, T(S_BALLOON), err_text(r)); balloon(t); g_failNotified = 1; }
    }
}
/* settings changed in the window but not applied yet are shown in bold, and Apply is enabled only when there
   is something to apply: a change, autostart to move to this exe, or a light that isn't the saved mode */
static void mark_changes(void) {
    HWND apply = g_wnd ? GetDlgItem(g_wnd, ID_APPLY) : NULL;
    int mode = (int)g_mode, dirty = 0, changed[7];
    static const int ids[7] = { ID_R0, ID_R0 + 1, ID_R0 + 2, ID_R3, ID_AUTOSTART, ID_ONWAKE, ID_RESPECT };
    if (!apply || !g_fontBold) return;
    for (int i = 0; i < 4; i++) if (IsDlgButtonChecked(g_wnd, ID_R0 + i) == BST_CHECKED) mode = i;
    for (int i = 0; i < 4; i++) changed[i] = i == mode && mode != (int)g_mode;
    int ticked = IsDlgButtonChecked(g_wnd, ID_AUTOSTART) == BST_CHECKED, on_now = autostart_enabled() != 0;
    changed[4] = ticked != on_now || (ticked && g_autoSwitch);  /* ticked and on, but Apply moves it to this exe */
    changed[5] = (IsDlgButtonChecked(g_wnd, ID_ONWAKE) == BST_CHECKED) != (g_onwake != 0);
    changed[6] = (IsDlgButtonChecked(g_wnd, ID_RESPECT) == BST_CHECKED) != (g_respect != 0);
    for (int i = 0; i < 7; i++) {
        HWND c = GetDlgItem(g_wnd, ids[i]); HFONT f = changed[i] ? g_fontBold : g_font;
        if (c && (HFONT)SendMessageW(c, WM_GETFONT, 0, 0) != f) SendMessageW(c, WM_SETFONT, (WPARAM)f, TRUE);
        dirty |= changed[i];
    }
    int on = dirty || (g_shownLevel >= 0 && g_shownLevel != (int)g_mode && (int)g_mode <= g_maxLevel);  /* the light isn't the saved mode: Fn+Space */
    if (!on && GetFocus() == apply) SetFocus(GetDlgItem(g_wnd, ID_R0 + mode));  /* a disabled button can't keep the focus */
    EnableWindow(apply, on);
}
static void sync_ui(void) {
    CheckRadioButton(g_wnd, ID_R0, ID_R3, ID_R0 + (int)g_mode);
    CheckDlgButton(g_wnd, ID_AUTOSTART, autostart_enabled() ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_wnd, ID_ONWAKE, g_onwake ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_wnd, ID_RESPECT, g_respect ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessageW(g_wnd, ID_LANG, CB_SETCURSEL, g_lang, 0);
    autostart_refresh();
    update_status();
    mark_changes();
}
static void apply_texts(void) {
    set_font();
    for (int i = 0; i < 4; i++) SetDlgItemTextW(g_wnd, ID_R0 + i, STR[g_lang][S_MODE0 + i]);
    SetDlgItemTextW(g_wnd, ID_GROUP, T(S_GROUP));
    SetDlgItemTextW(g_wnd, ID_AUTOSTART, T(S_AUTOSTART));
    SetDlgItemTextW(g_wnd, ID_ONWAKE, T(S_ONWAKE));
    SetDlgItemTextW(g_wnd, ID_RESPECT, T(S_RESPECT));
    SetDlgItemTextW(g_wnd, ID_TEST, T(S_TEST));
    SetDlgItemTextW(g_wnd, ID_APPLY, T(S_APPLY));
    SetDlgItemTextW(g_wnd, ID_HIDE, T(S_HIDE));
    SetDlgItemTextW(g_wnd, ID_LANGLBL, T(S_LANG));
    SetDlgItemTextW(g_wnd, ID_ABOUT, T(S_ABOUT));
    SetDlgItemTextW(g_wnd, ID_UPDATE, g_updBusy ? T(S_UPD_CHECKING) : T(S_UPD_BUTTON));
    layout();
    update_status();
    mark_changes();  /* set_font made every control normal again */
}
static void dpi_changed(int dpi, const RECT *suggested);
static void show_window(void) {
    /* the scale may have changed while hidden (docking, display settings): lay out for the current one */
    if (p_GetDpiForWindow) { UINT d = p_GetDpiForWindow(g_wnd); if (d && (int)d != g_dpi) dpi_changed((int)d, NULL); }
    sync_ui(); ShowWindow(g_wnd, SW_SHOW); ShowWindow(g_wnd, SW_RESTORE); SetForegroundWindow(g_wnd);
}
static HWND mk(const wchar_t *cls, DWORD st, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(w), S(h), g_wnd, (HMENU)(INT_PTR)id, g_inst, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE); return c;
}
static void build_ui(void) {
    SetWindowSubclass(mk(L"BUTTON", BS_GROUPBOX, 12, 8, 316, 128, ID_GROUP), group_proc, 1, 0);
    for (int i = 0; i < 4; i++) mk(L"BUTTON", BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP | WS_TABSTOP : 0), 28, 30 + i * 25, 280, 22, ID_R0 + i);
    mk(L"BUTTON", BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP | WS_GROUP, 16, 146, 324, 22, ID_AUTOSTART);
    mk(L"BUTTON", BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 16, 172, 324, 22, ID_ONWAKE);
    mk(L"BUTTON", BS_AUTOCHECKBOX | BS_MULTILINE | WS_TABSTOP, 16, 198, 324, 36, ID_RESPECT);
    mk(L"STATIC", SS_LEFT, 16, 240, 324, 22, ID_STATUS);
    mk(L"STATIC", SS_LEFT | SS_CENTERIMAGE, 16, 270, 90, 24, ID_LANGLBL);
    HWND cb = mk(L"COMBOBOX", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, 108, 268, 116, 200, ID_LANG);
    mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 232, 266, 96, 28, ID_ABOUT);
    mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 196, 300, 132, 28, ID_UPDATE);
    for (int i = 0; i < L_COUNT; i++) SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)LANG_NAMES[i]);
    mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 12, 308, 100, 30, ID_TEST);
    mk(L"BUTTON", BS_DEFPUSHBUTTON | WS_TABSTOP, 128, 308, 96, 30, ID_APPLY);
    mk(L"BUTTON", BS_PUSHBUTTON | WS_TABSTOP, 232, 308, 96, 30, ID_HIDE);
    apply_texts();
}
static void add_tray(void) {
    ZeroMemory(&g_nid, sizeof g_nid);
    g_nid.cbSize = sizeof g_nid; g_nid.hWnd = g_wnd; g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = tray_icon();  /* loaded at the small icon size: LoadIcon would shrink the 32 px frame */
    wcscpy(g_nid.szTip, L"Glim");
    g_trayShown = NULL; g_trayTipShown[0] = 0;
    if (tray_send(NIM_ADD) || tray_send(NIM_MODIFY)) g_trayRetries = 0;
    else if (g_trayRetries < 30) { g_trayRetries++; SetTimer(g_wnd, T_TRAY, 2000, NULL); }  /* tray not ready yet: try again */
}
static void tray_menu(void) {
    POINT p; GetCursorPos(&p);
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_TRAY_OPEN, T(S_OPEN));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < 4; i++) AppendMenuW(m, MF_STRING | (eff_mode() == i ? MF_CHECKED : 0) | (i > g_maxLevel ? MF_GRAYED : 0), ID_TRAY_OFF + i, STR[g_lang][S_MODE0 + i]);
    if (g_respect && g_override >= 0) {
        wchar_t t[160]; swprintf(t, 160, T(S_RESTORE), STR[g_lang][S_MODE0 + g_mode]);
        AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, ID_TRAY_RESTORE, t);
    }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_updBusy ? MF_GRAYED : 0), ID_TRAY_UPDATE, T(S_UPD_BUTTON));
    AppendMenuW(m, MF_STRING, ID_TRAY_ABOUT, T(S_ABOUT));
    AppendMenuW(m, MF_STRING, ID_TRAY_EXIT, T(S_EXIT));
    SetMenuDefaultItem(m, ID_TRAY_OPEN, FALSE);
    SetForegroundWindow(g_wnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, p.x, p.y, 0, g_wnd, NULL);
    PostMessageW(g_wnd, WM_NULL, 0, 0);
    DestroyMenu(m);
}

/* ---------- driver check on manual launch ---------- */
static void check_driver_dialog(void) {
    int r = backlight(-1);
    if (r == -1) { klog(L"driver check: no Lenovo driver (err=%lu)", g_lastErr); MessageBoxW(g_wnd, T(S_DRV_MISSING), T(S_DRV_TITLE), MB_OK | MB_ICONWARNING); }
    else if (r == -3) { klog(L"driver check: access denied"); MessageBoxW(g_wnd, T(S_DRV_DENIED), T(S_DRV_TITLE), MB_OK | MB_ICONWARNING); }
    else if (r < 0) klog(L"driver check: unsupported (err=%lu)", g_lastErr);
    else klog(L"driver check: OK, current %ls", LOG_MODE[r]);
}

/* ---------- self-test ---------- */
static int self_test(void) {  /* returns the number of failures */
    static wchar_t rep[6144]; wchar_t line[400]; int fails = 0, warns = 0; rep[0] = 0;
#define ADD(...) do { swprintf(line, 400, __VA_ARGS__); wcscat(rep, line); wcscat(rep, L"\r\n"); } while (0)
    int cur = backlight(-1), ok = cur >= 0 && cur <= 3;
    if (ok) ADD(T(S_T_DRVOK), STR[g_lang][S_MODE0 + cur]); else { ADD(T(S_T_DRVFAIL), err_text(cur), g_lastErr); fails++; }
    if (g_raw[0]) ADD(T(S_T_RAW), DRV_NAME[g_rawDrv], g_raw);  /* for reports of new models */
    if (ok) {
        int tried = 0, changed = 0;  /* modes other than the one it was in */
        for (int lv = 0; lv <= 3; lv++) {
            if (lv > g_maxLevel) { ADD(T(S_T_NOMODE), STR[g_lang][S_MODE0 + lv]); continue; }
            int r = backlight(lv); Sleep(250); int rb = backlight(-1);
            if (lv != cur) { tried++; changed += r == lv && rb == lv; }
            if (r != lv) { ADD(T(S_T_SETFAIL), STR[g_lang][S_MODE0 + lv]); fails++; }
            else if (rb != lv) { ADD(T(S_T_SETWARN), STR[g_lang][S_MODE0 + lv], rb); warns++; }
            else ADD(T(S_T_SETOK), STR[g_lang][S_MODE0 + lv]);
        }
        if (tried && !changed) { ADD(L"%ls", T(S_T_NOEFFECT)); fails++; }
        int m = eff_mode(), back = m <= g_maxLevel ? m : cur;  /* a saved mode it doesn't have (said above): the one before the test */
        int r = backlight(back);
        if (r == back) ADD(T(S_T_RESTOK), STR[g_lang][S_MODE0 + back]); else { ADD(T(S_T_RESTFAIL), STR[g_lang][S_MODE0 + back]); fails++; }
        if (back != m) warns++;
        r = backlight(-1); if (r >= 0 && r <= 3) g_lastSet = g_lastSeen = r;  /* whatever it is now isn't a Fn+Space */
    }
    DWORD keep = g_mode; save_settings(); g_mode = 99; load_settings();
    if (g_mode == keep) ADD(L"%ls", T(S_T_REGOK)); else { ADD(L"%ls", T(S_T_REGFAIL)); fails++; g_mode = keep; }
    wchar_t p[MAX_PATH];
    if (run_target(p)) {
        if (p[0] && GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES) ADD(T(S_T_AUTOOK), p); else { ADD(L"%ls", T(S_T_AUTOFAIL)); fails++; }
    } else ADD(L"%ls", T(S_T_AUTOOFF));
    wcscat(rep, L"\r\n");
    ADD(T(S_T_SUM), fails ? T(S_T_BAD) : T(S_T_GOOD), fails, warns);
    ADD(T(S_T_LOG), g_dir);
#undef ADD
    klog(L"self-test: fails=%d warns=%d", fails, warns);
    wchar_t rp[MAX_PATH]; swprintf(rp, MAX_PATH, L"%ls\\selftest.txt", g_dir);
    HANDLE f = CreateFileW(rp, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { static char u8[20000]; int n = WideCharToMultiByte(CP_UTF8, 0, rep, -1, u8, sizeof u8, NULL, NULL);
        DWORD w; if (n > 1) WriteFile(f, u8, (DWORD)(n - 1), &w, NULL); CloseHandle(f); }
    if (!g_quiet) MessageBoxW(g_wnd, rep, T(S_T_TITLE), MB_OK | (fails ? MB_ICONERROR : warns ? MB_ICONWARNING : MB_ICONINFORMATION));
    if (g_wnd) update_status();
    return fails;
}


/* ---------- about ---------- */
static HRESULT CALLBACK about_cb(HWND h, UINT n, WPARAM w, LPARAM l, LONG_PTR ref) {
    (void)w; (void)ref;
    if (n == TDN_HYPERLINK_CLICKED) ShellExecuteW(h, L"open", (LPCWSTR)l, NULL, NULL, SW_SHOWNORMAL);
    return S_OK;
}
static void show_about(void) {
    static wchar_t content[1024], main_[64];
    swprintf(main_, 64, L"Glim %ls", APP_VERSION);
    swprintf(content, 1024, L"%ls\n\n<a href=\"%ls\">%ls</a>", T(S_ABOUT_TEXT), APP_URL, APP_URL + 8 /* without https:// */);
    TASKDIALOGCONFIG tc = { sizeof tc };
    tc.hwndParent = g_wnd; tc.hInstance = g_inst;
    tc.dwFlags = TDF_ENABLE_HYPERLINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_POSITION_RELATIVE_TO_WINDOW;
    tc.dwCommonButtons = TDCBF_OK_BUTTON;
    tc.pszWindowTitle = T(S_ABOUT_TITLE);
    tc.pszMainIcon = MAKEINTRESOURCEW(1);
    tc.pszMainInstruction = main_;
    tc.pszContent = content;
    tc.pszFooter = T(S_ABOUT_FOOT);
    tc.pfCallback = about_cb;
    tc.cxWidth = 280;  /* dialog units: keeps the link on one line */
    if (FAILED(TaskDialogIndirect(&tc, NULL, NULL, NULL))) {
        wchar_t t[1400]; swprintf(t, 1400, L"%ls\n\n%ls\n\n%ls", main_, T(S_ABOUT_TEXT), APP_URL);
        if (MessageBoxW(g_wnd, t, T(S_ABOUT_TITLE), MB_OKCANCEL | MB_ICONINFORMATION) == IDOK)
            ShellExecuteW(g_wnd, L"open", APP_URL, NULL, NULL, SW_SHOWNORMAL);
    }
}

/* ---------- updates (only when the user asks: button or tray menu) ---------- */
/* Check: GitHub API "latest release" -> its tag, notes and that its Glim.exe is there. A newer version: the user
   is offered the release page in the browser and runs the new exe - it closes this copy (take_over) and autostart
   is switched to it by Apply. Glim doesn't download or start executables itself: antivirus behaviour monitors take
   a program that downloads an exe, replaces itself and starts the new one for a trojan. */
enum { UPD_OK, UPD_E_NET, UPD_E_FORMAT, UPD_E_LIMIT, UPD_E_NOFILE };
typedef struct {
    int err; DWORD detail;            /* detail: HTTP status or Windows error, for the log */
    ULONGLONG ver; wchar_t tag[32];   /* the latest release */
    wchar_t notes[1200];
} upd_t;
static upd_t *g_upd;  /* the check in progress; messages carrying any other pointer are ignored */
static int g_updTest = 0;  /* test builds only (GLIM_TEST_BUILD, GLIM_FAKE + GLIM_UPDATE_AUTO): another API URL, plain http, no dialogs */

static const wchar_t *upd_api(void) { static wchar_t u[512]; return g_updTest && GetEnvironmentVariableW(L"GLIM_UPDATE_API", u, 512) ? u : UPD_API; }

/* GET into a malloc'ed buffer (NUL-terminated); 0 = failed, *detail = HTTP status or Windows error,
   *answered = the server answered (with any status) */
static int http_get(const wchar_t *url, const wchar_t *accept, char **out, DWORD *size, DWORD max, DWORD *detail, int *answered) {
    wchar_t host[256], path[2048], extra[1024];
    URL_COMPONENTS uc; ZeroMemory(&uc, sizeof uc); uc.dwStructSize = sizeof uc;
    uc.lpszHostName = host; uc.dwHostNameLength = 256; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = 1024;
    *out = NULL; *size = 0; *detail = 0; *answered = 0;
    if (!g_haveHttp) { *detail = ERROR_MOD_NOT_FOUND; return 0; }
    if (!p_WinHttpCrackUrl(url, 0, 0, &uc)) { *detail = GetLastError(); return 0; }
    if (uc.nScheme != INTERNET_SCHEME_HTTPS && !(g_updTest && uc.nScheme == INTERNET_SCHEME_HTTP)) { *detail = ERROR_ACCESS_DENIED; return 0; }
    if (wcslen(path) + wcslen(extra) >= 2048) { *detail = ERROR_BUFFER_OVERFLOW; return 0; }
    wcscat(path, extra);
    const wchar_t *ua = L"Glim/" APP_VERSION;
    HINTERNET ses = p_WinHttpOpen(ua, 4 /* WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, Windows 8.1+ */, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) ses = p_WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { *detail = GetLastError(); return 0; }
    p_WinHttpSetTimeouts(ses, 10000, 10000, 15000, 30000);
    HINTERNET con = p_WinHttpConnect(ses, host, uc.nPort, 0), req = NULL;
    int ok = 0; char *buf = NULL; DWORD n = 0, cap = 0;
    if (con) req = p_WinHttpOpenRequest(con, L"GET", path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    if (req && accept) p_WinHttpAddRequestHeaders(req, accept, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
    if (req && p_WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && p_WinHttpReceiveResponse(req, NULL)) {
        DWORD code = 0, len = sizeof code;
        *answered = 1;
        if (!p_WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX) || code != 200)
            *detail = code ? code : GetLastError();
        else for (;;) {
            DWORD avail = 0, got = 0;
            if (!p_WinHttpQueryDataAvailable(req, &avail)) { *detail = GetLastError(); break; }
            if (!avail) { ok = 1; break; }
            if (n + avail > max) { *detail = ERROR_FILE_TOO_LARGE; break; }
            if (n + avail + 1 > cap) { DWORD c2 = (n + avail + 1) * 2; if (c2 > max + 1) c2 = max + 1; char *b2 = realloc(buf, c2); if (!b2) { *detail = ERROR_OUTOFMEMORY; break; } buf = b2; cap = c2; }
            if (!p_WinHttpReadData(req, buf + n, avail, &got)) { *detail = GetLastError(); break; }
            n += got;
        }
    } else if (!*detail) *detail = GetLastError();
    if (req) p_WinHttpCloseHandle(req);
    if (con) p_WinHttpCloseHandle(con);
    p_WinHttpCloseHandle(ses);
    if (!ok) { free(buf); return 0; }
    if (!buf) { buf = malloc(1); if (!buf) return 0; }
    buf[n] = 0; *out = buf; *size = n; return 1;
}

/* a small JSON reader: just enough to take string fields from GitHub's release object */
static const char *js_ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; return p; }
static const char *js_str_end(const char *p, const char *e) {  /* p at '"'; returns just past the closing quote */
    for (p++; p < e; p++) { if (*p == '\\') { p++; continue; } if (*p == '"') return p + 1; }
    return NULL;
}
static const char *js_skip(const char *p, const char *e) {  /* returns just past the value at p */
    p = js_ws(p, e);
    if (p >= e) return NULL;
    if (*p == '"') return js_str_end(p, e);
    if (*p == '{' || *p == '[') {
        int depth = 0;
        while (p < e) {
            if (*p == '"') { p = js_str_end(p, e); if (!p) return NULL; continue; }
            if (*p == '{' || *p == '[') depth++;
            else if ((*p == '}' || *p == ']') && --depth == 0) return p + 1;
            p++;
        }
        return NULL;
    }
    while (p < e && *p != ',' && *p != '}' && *p != ']') p++;  /* number, true, false, null */
    return p;
}
static const char *js_get(const char *p, const char *e, const char *key) {  /* value of a member of the object at p */
    size_t kl = strlen(key);
    p = js_ws(p, e);
    if (p >= e || *p != '{') return NULL;
    for (p++;;) {
        p = js_ws(p, e);
        if (p >= e || *p != '"') return NULL;
        const char *ks = p + 1, *ke = js_str_end(p, e);
        if (!ke) return NULL;
        p = js_ws(ke, e);
        if (p >= e || *p != ':') return NULL;
        p = js_ws(p + 1, e);
        if ((size_t)(ke - 1 - ks) == kl && !memcmp(ks, key, kl)) return p;
        if (!(p = js_skip(p, e))) return NULL;
        p = js_ws(p, e);
        if (p >= e || *p != ',') return NULL;
        p++;
    }
}
static int js_hex(const char *p, unsigned *v) {
    *v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i]; unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (unsigned)(c - 'A' + 10) : 16u;
        if (d > 15) return 0;
        *v = *v * 16 + d;
    }
    return 1;
}
/* the JSON string at p as UTF-16, cut to cap; 0 = not a string */
static int js_wstr(const char *p, const char *e, wchar_t *out, size_t cap) {
    size_t n = 0;
    if (!p || p >= e || *p != '"' || !cap) return 0;
    for (p++; p < e && *p != '"';) {
        unsigned c = (unsigned char)*p++, cp, more = 0;
        if (c == '\\') {
            if (p >= e) return 0;
            char x = *p++;
            if (x == 'u') { if (e - p < 4 || !js_hex(p, &cp)) return 0; p += 4; }  /* surrogates pass through as they are */
            else cp = x == 'n' ? '\n' : x == 't' ? '\t' : x == 'r' ? '\r' : x == 'b' ? 8 : x == 'f' ? 12 : (unsigned char)x;
        } else if (c < 0x80) cp = c;
        else {
            if (c >= 0xF0 && c < 0xF8) { cp = c & 7; more = 3; } else if (c >= 0xE0) { cp = c & 15; more = 2; } else if (c >= 0xC0) { cp = c & 31; more = 1; } else return 0;
            while (more--) { if (p >= e || ((unsigned char)*p & 0xC0) != 0x80) return 0; cp = cp << 6 | ((unsigned char)*p++ & 0x3F); }
        }
        if (cp >= 0x10000) {
            if (n + 2 < cap) { cp -= 0x10000; out[n++] = (wchar_t)(0xD800 + (cp >> 10)); out[n++] = (wchar_t)(0xDC00 + (cp & 0x3FF)); }
        } else if (n + 1 < cap) out[n++] = (wchar_t)cp;
    }
    if (p >= e) return 0;
    out[n] = 0;
    return 1;
}
static ULONGLONG tag_version(const wchar_t *t) {  /* "v1.2" or "v1.2.3" -> the APP_FILEVER form; 0 = not a version */
    unsigned v[3] = { 0, 0, 0 }; int parts = 0;
    if (*t == L'v' || *t == L'V') t++;
    while (parts < 3) {
        if (*t < L'0' || *t > L'9') return 0;
        while (*t >= L'0' && *t <= L'9') { v[parts] = v[parts] * 10 + (unsigned)(*t - L'0'); if (v[parts] > 65535) return 0; t++; }
        parts++;
        if (*t == L'.') { t++; continue; }
        break;
    }
    if (*t || parts < 2) return 0;
    return (((ULONGLONG)v[0] << 16 | v[1]) << 32) | ((ULONGLONG)v[2] << 16);
}
static void parse_release(const char *js, DWORD n, upd_t *u) {
    const char *e = js + n, *a, *tag = js_get(js, e, "tag_name");
    u->err = UPD_E_FORMAT;
    if (!js_wstr(tag, e, u->tag, 32) || !(u->ver = tag_version(u->tag))) return;
    if (!js_wstr(js_get(js, e, "body"), e, u->notes, 1200)) u->notes[0] = 0;
    if (wcslen(u->notes) >= 1190) wcscpy(u->notes + 1190, L"…");
    if (!(a = js_get(js, e, "assets")) || *a != '[') return;
    for (a++;;) {
        a = js_ws(a, e);
        if (a >= e) return;
        if (*a == ']') { u->err = UPD_E_NOFILE; return; }  /* published, but Glim.exe isn't uploaded (yet) */
        wchar_t name[64];
        if (js_wstr(js_get(a, e, "name"), e, name, 64) && !wcscmp(name, L"Glim.exe")) { if (js_skip(a, e)) u->err = UPD_OK; return; }  /* not a cut answer */
        if (!(a = js_skip(a, e))) return;
        a = js_ws(a, e);
        if (a >= e || *a != ',') { if (a < e && *a == ']') u->err = UPD_E_NOFILE; return; }
        a++;
    }
}
static int http_limited(DWORD detail) { return detail == 403 || detail == 429; }  /* GitHub's rate limit */
static DWORD WINAPI upd_check_thread(LPVOID arg) {
    upd_t *u = arg; char *js; DWORD n; int answered;
    if (!http_get(upd_api(), L"Accept: application/vnd.github+json", &js, &n, 4 * 1024 * 1024, &u->detail, &answered))
        u->err = !answered ? UPD_E_NET : http_limited(u->detail) ? UPD_E_LIMIT : UPD_E_FORMAT;  /* e.g. 404: no published release */
    else { parse_release(js, n, u); free(js); }
    PostMessageW(g_wnd, WM_GLIM_UPD_INFO, 0, (LPARAM)u);  /* not delivered = the window is gone, the process ends */
    return 0;
}

static const wchar_t *upd_reason(int err) {
    return err == UPD_E_NET ? T(S_UPD_E_NET) : err == UPD_E_LIMIT ? T(S_UPD_E_LIMIT) : err == UPD_E_NOFILE ? T(S_UPD_E_NOFILE) : T(S_UPD_E_FORMAT);
}
static void upd_busy(int busy) {
    g_updBusy = busy;
    if (!g_wnd) return;
    SetDlgItemTextW(g_wnd, ID_UPDATE, busy ? T(S_UPD_CHECKING) : T(S_UPD_BUTTON));
    EnableWindow(GetDlgItem(g_wnd, ID_UPDATE), !busy);
}
static void upd_say(const wchar_t *text, UINT icon) {  /* tests: to the log instead of a box */
    if (g_updTest) klog(L"update says: %ls", text);
    else MessageBoxW(IsWindowVisible(g_wnd) ? g_wnd : NULL, text, T(S_UPD_TITLE), MB_OK | icon);
}
static void upd_done(upd_t *u) { if (u == g_upd) g_upd = NULL; free(u); upd_busy(0); }
static void upd_fail(upd_t *u) {
    wchar_t t[300];
    klog(L"update: failed (%d, %lu)", u->err, u->detail);
    swprintf(t, 300, T(S_UPD_FAIL), upd_reason(u->err));
    upd_done(u);
    upd_say(t, MB_ICONWARNING);
}
static void upd_start_check(void) {
    if (g_updBusy) return;
    upd_t *u = calloc(1, sizeof *u);
    if (!u) return;
    load_net_dlls();
    klog(L"update: checking");
    upd_busy(1); g_upd = u;
    HANDLE th = CreateThread(NULL, 0, upd_check_thread, u, 0, NULL);
    if (th) CloseHandle(th); else upd_done(u);
}
static void upd_on_info(upd_t *u) {  /* UI thread: the check finished */
    wchar_t t[2400], self[MAX_PATH];
    if (u->err) { upd_fail(u); return; }
    klog(L"update: latest release %ls", u->tag);
    if (u->ver <= APP_FILEVER) {
        swprintf(t, 2400, T(S_UPD_LATEST), APP_VERSION);
        upd_done(u); upd_say(t, MB_ICONINFORMATION); return;
    }
    self_path(self);  /* saved over this file, the new version keeps autostart where it is (not in Downloads) */
    swprintf(t, 2400, T(S_UPD_ASK), u->tag[0] == L'v' ? u->tag + 1 : u->tag, APP_VERSION, u->notes, self);
    klog(L"update: %ls is available, to be saved over %ls", u->tag, self);
    if (g_updTest) klog(L"update asks: %ls", t);
    if (!g_updTest && MessageBoxW(IsWindowVisible(g_wnd) ? g_wnd : NULL, t, T(S_UPD_TITLE), MB_YESNO | MB_ICONQUESTION) != IDYES) {
        klog(L"update: declined"); upd_done(u); return;
    }
    wchar_t page[256];  /* the tag is digits and dots (tag_version): nothing else gets into the URL */
    swprintf(page, 256, L"%ls/releases/tag/%ls", APP_URL, u->tag);
    klog(L"update: opening %ls", page);
    if (!g_updTest) ShellExecuteW(g_wnd, L"open", page, NULL, NULL, SW_SHOWNORMAL);
    upd_done(u);
}
/* ---------- wake ---------- */
/* firmware may reset the backlight while asleep/hibernated: changes in the grace window are reverted, not treated as Fn+Space */
static void wake_event(const wchar_t *what, UINT reapply_ms) {
    klog(L"%ls: watching for a firmware reset", what);
    begin_grace(ms(15000));
    SetTimer(g_wnd, T_POLL, ms(500) < 100 ? 100 : ms(500), NULL);  /* check quickly, then the poll returns to normal */
    if (g_onwake) SetTimer(g_wnd, T_WAKE, ms(reapply_ms), NULL);
}
/* only "on after off" is a wake: "on after dimmed" happens in normal use, and the first notification
   right after registration just reports the current state */
static void display_state(DWORD st) {
    int prev = g_display; g_display = (int)st;
    if (st == 1 && prev == 0) wake_event(L"screen on", 2000);
}

/* ---------- window ---------- */
/* moved to a monitor with another scale, or the scale changed (per-monitor DPI v2): fonts, icons and layout for it */
static void dpi_changed(int dpi, const RECT *suggested) {
    if (dpi < 48 || dpi > 1536 || !g_wnd) return;
    g_dpi = dpi;
    make_fonts();
    set_window_icons();
    if (suggested) SetWindowPos(g_wnd, NULL, suggested->left, suggested->top, suggested->right - suggested->left,
                                suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
    layout();  /* the height follows the texts, as at start */
    mark_changes();
}
static LRESULT test_state(WPARAM what) {  /* WM_GLIM_TEST_STATE */
    if (what == 0) {  /* which icon the taskbar accepted */
        for (int i = 0; i < 4; i++) if (g_trayIcon[i] && g_trayShown == g_trayIcon[i]) return i;
        return g_trayShown == g_smallIcon ? 4 : 5;
    }
    if (what == 4) { LOGFONTW lf; return GetObjectW(g_font, sizeof lf, &lf) ? abs(lf.lfHeight) : 0; }
    if (what == 5) {  /* the fonts the controls really have, and Apply */
        static const int ids[7] = { ID_R0, ID_R0 + 1, ID_R0 + 2, ID_R3, ID_AUTOSTART, ID_ONWAKE, ID_RESPECT };
        int r = IsWindowEnabled(GetDlgItem(g_wnd, ID_APPLY)) ? 128 : 0;
        for (int i = 0; i < 7; i++) if ((HFONT)SendDlgItemMessageW(g_wnd, ids[i], WM_GETFONT, 0, 0) == g_fontBold) r |= 1 << i;
        return r;
    }
    if (what == 6) {  /* the driver, its highest mode and the mode radios enabled */
        int r = g_drv * 100 + g_maxLevel * 10, n = 0;
        for (int i = 0; i < 4; i++) n += IsWindowEnabled(GetDlgItem(g_wnd, ID_R0 + i)) != 0;
        return r + n;
    }
    return what == 1 ? g_dark : what == 2 ? g_dpi : what == 3 ? g_customDraws : 0;
}
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_taskbarCreated && msg == g_taskbarCreated) { add_tray(); update_status(); return 0; }
    switch (msg) {
    case WM_ACTIVATE:  /* the autostart target or this exe may have been moved meanwhile */
        if (LOWORD(wp) != WA_INACTIVE && IsWindowVisible(h)) { autostart_refresh(); mark_changes(); }
        break;
    case WM_ERASEBKGND:
        if (g_dark) { RECT r; GetClientRect(h, &r); FillRect((HDC)wp, &r, g_darkBg); return 1; }
        break;
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN:  /* labels, the status line; backgrounds of the buttons */
        if (g_dark) { SetTextColor((HDC)wp, IsWindowEnabled((HWND)lp) ? DARK_TEXT : DARK_DIM); SetBkColor((HDC)wp, DARK_BG); return (LRESULT)g_darkBg; }
        break;
    case WM_CTLCOLORLISTBOX:  /* the language list */
        if (g_dark) { SetTextColor((HDC)wp, DARK_TEXT); SetBkColor((HDC)wp, DARK_LIST); return (LRESULT)g_darkList; }
        break;
    case WM_NOTIFY: {
        const NMHDR *nh = (const NMHDR*)lp;
        if (g_dark && nh && nh->code == NM_CUSTOMDRAW && GetParent(nh->hwndFrom) == h) {
            UINT_PTR id = nh->idFrom;
            if ((id >= ID_R0 && id <= ID_R3) || id == ID_AUTOSTART || id == ID_ONWAKE || id == ID_RESPECT) return draw_check((const NMCUSTOMDRAW*)lp);
        }
        break;
    }
    case WM_SETTINGCHANGE: case WM_SYSCOLORCHANGE: case WM_THEMECHANGED:  /* app mode, high contrast, visual style */
        if (dark_wanted() != g_dark) { g_dark = !g_dark; klog(L"dark mode %ls", g_dark ? L"on" : L"off"); apply_theme(); }
        break;
    case WM_DPICHANGED: dpi_changed(LOWORD(wp), (const RECT*)lp); return 0;
    case WM_GLIM_TEST_DPI:  /* as Windows sends it: the window scaled at its top-left */
        if (g_fake[0] && wp >= 48 && wp <= 1536) {
            RECT wr, rc; GetWindowRect(h, &wr);
            rc.left = wr.left; rc.top = wr.top;
            rc.right = wr.left + MulDiv(wr.right - wr.left, (int)wp, g_dpi); rc.bottom = wr.top + MulDiv(wr.bottom - wr.top, (int)wp, g_dpi);
            SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(wp, wp), (LPARAM)&rc);
        }
        return 0;
    case WM_GLIM_TEST_STATE: return g_fake[0] ? test_state(wp) : 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_APPLY:
            for (int i = 0; i < 4; i++)
                if (IsDlgButtonChecked(h, ID_R0 + i) == BST_CHECKED && IsWindowEnabled(GetDlgItem(h, ID_R0 + i))) g_mode = (DWORD)i;
            g_onwake = IsDlgButtonChecked(h, ID_ONWAKE) == BST_CHECKED;
            g_respect = IsDlgButtonChecked(h, ID_RESPECT) == BST_CHECKED;
            g_override = -1;
            save_settings(); g_failNotified = g_startRetrying = 0; apply_saved(L"manual"); update_status();
            autostart_apply(IsDlgButtonChecked(h, ID_AUTOSTART) == BST_CHECKED);  /* last: it may show a box */
            CheckDlgButton(h, ID_AUTOSTART, autostart_enabled() ? BST_CHECKED : BST_UNCHECKED);
            autostart_refresh();
            mark_changes();
            return 0;
        case ID_R0: case ID_R0 + 1: case ID_R0 + 2: case ID_R3: case ID_AUTOSTART: case ID_ONWAKE: case ID_RESPECT:
            if (HIWORD(wp) == BN_CLICKED) mark_changes();
            return 0;
        case ID_LANG:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                LRESULT sel = SendDlgItemMessageW(h, ID_LANG, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < L_COUNT) { g_lang = (DWORD)sel; save_settings(); apply_texts(); }
            }
            return 0;
        case ID_HIDE: case IDCANCEL: ShowWindow(h, SW_HIDE); return 0;  /* IDCANCEL: Esc */
        case ID_TEST: self_test(); return 0;
        case ID_ABOUT: case ID_TRAY_ABOUT: show_about(); return 0;
        case ID_UPDATE: case ID_TRAY_UPDATE: upd_start_check(); return 0;
        case ID_TRAY_OPEN: show_window(); return 0;
        case ID_TRAY_OFF: case ID_TRAY_LOW: case ID_TRAY_HIGH: case ID_TRAY_AUTO:
            if (LOWORD(wp) - ID_TRAY_OFF > g_maxLevel) { klog(L"tray: %ls isn't available on this keyboard", LOG_MODE[LOWORD(wp) - ID_TRAY_OFF]); return 0; }
            g_mode = (DWORD)(LOWORD(wp) - ID_TRAY_OFF); g_override = -1; save_settings(); g_failNotified = g_startRetrying = 0; apply_saved(L"tray"); update_status();
            if (IsWindowVisible(h)) { CheckRadioButton(h, ID_R0, ID_R3, ID_R0 + (int)g_mode); mark_changes(); }  /* check box edits stay */
            return 0;
        case ID_TRAY_RESTORE:
            g_override = -1; klog(L"manual override cleared from tray"); apply_saved(L"restore"); update_status();
            if (IsWindowVisible(h)) mark_changes();
            return 0;
        case ID_TRAY_EXIT: DestroyWindow(h); return 0;
        }
        break;
    case WM_TRAY:
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_LBUTTONDBLCLK) show_window();
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) tray_menu();
        return 0;
    case WM_SHOWME: show_window(); return 0;
    case WM_GLIM_VERSION: return (LRESULT)APP_FILEVER;
    case WM_GLIM_SELFTEST: {  /* "Glim.exe /selftest" while this copy is running: test here, so the poll doesn't take the test for Fn+Space */
        int q = g_quiet; g_quiet = wp != 0; int fails = self_test(); g_quiet = q;
        return fails ? 2 : 1;
    }
    case WM_GLIM_TEST_DISPLAY: if (g_fake[0]) display_state((DWORD)wp); return 0;
    case WM_GLIM_UPD_INFO: if (lp && (upd_t*)lp == g_upd) upd_on_info(g_upd); return 0;
    case WM_TIMER:
        if (wp == T_POLL) {
            /* detect a sleep/hibernation gap even if the poll fires before the power notification */
            static ULONGLONG lastWall = 0, lastRun = 0;
            FILETIME ft; GetSystemTimeAsFileTime(&ft);
            ULONGLONG wall = (((ULONGLONG)ft.dwHighDateTime << 32) | ft.dwLowDateTime) / 10000, run = 0;
            QueryUnbiasedInterruptTime(&run); run /= 10000;
            if (lastWall && wall > lastWall && (wall - lastWall) > (run - lastRun) + 3000) {
                klog(L"sleep/hibernation gap detected (%llu s)", (unsigned long long)((wall - lastWall) / 1000));
                begin_grace(ms(15000));
            }
            lastWall = wall; lastRun = run;
            SetTimer(h, T_POLL, ms(2000) < 200 ? 200 : ms(2000), NULL);

            int cur = backlight(-1);
            if (cur < 0 || cur > 3) { if (g_trayLevel >= 0) show_status(cur); return 0; }  /* the driver stopped answering: say so */
            int exp = eff_mode();
            if (GetTickCount64() < g_graceUntil) {
                /* system window (after login / wake): revert any change made by firmware or Lenovo apps
                   (not to a saved mode this keyboard doesn't have: the failure balloon asked to pick another) */
                if (cur != exp && exp <= g_maxLevel) {
                    int r = backlight(exp);
                    klog(L"system changed backlight to %ls, restored %ls%ls", LOG_MODE[cur], LOG_MODE[exp], r == exp ? L"" : L" - FAILED");
                    if (r == exp) cur = exp;
                    else if (g_graceRetries < 5 && GetTickCount64() + ms(4000) > g_graceUntil) {
                        g_graceRetries++; start_grace(ms(4000));  /* don't let the window end on a failed revert */
                    }
                }
                if (cur == exp || exp > g_maxLevel) g_lastSet = cur;
                g_lastSeen = cur;
                show_status(cur);
                return 0;
            }
            if (g_lastSet < 0) {
                /* the driver wasn't available until now: this level isn't a user's Fn+Space */
                if (g_startup) { apply_saved(L"driver available"); if (g_lastSet >= 0) begin_grace(ms(15000)); }
                if (g_lastSet < 0) { g_lastSet = g_lastSeen = cur; klog(L"driver available, current %ls", LOG_MODE[cur]); }
                update_status();
                return 0;
            }
            if (cur == g_lastSeen) { if (cur != g_trayLevel) show_status(cur); return 0; }  /* e.g. the driver answers again */
            if (g_display == 0) {  /* screen off: the firmware, not Fn+Space (a key turns the screen on first) - the
                                      screen-on window restores the mode */
                klog(L"screen off: backlight changed to %ls - not taken for a manual change", LOG_MODE[cur]);
                g_lastSeen = cur; show_status(cur);
                return 0;
            }
            g_lastSeen = cur;
            if (cur != g_lastSet && g_respect) {
                g_override = (cur == (int)g_mode) ? -1 : cur;
                g_lastSet = cur;
                klog(L"manual change detected: %ls%ls", LOG_MODE[cur], g_override >= 0 ? L" (kept until restart)" : L" (= saved mode)");
            }
            show_status(cur);
            return 0;
        }
        KillTimer(h, wp);
        if (wp == T_TRAY) { add_tray(); update_status(); return 0; }
        if (wp == T_START3) g_startRetrying = 0;
        apply_saved(wp == T_WAKE ? L"wake/screen on" : L"startup retry");
        if (IsWindowVisible(h)) update_status();
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) wake_event(L"resume", 3000);
        else if (wp == PBT_POWERSETTINGCHANGE) {
            POWERBROADCAST_SETTING *ps = (POWERBROADCAST_SETTING*)lp;
            if (ps && IsEqualGUID(&ps->PowerSetting, &MY_GUID_CONSOLE_DISPLAY_STATE) && ps->DataLength >= sizeof(DWORD)) {
                DWORD st; memcpy(&st, ps->Data, sizeof st); display_state(st);
            }
        }
        return TRUE;
    case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    case WM_DESTROY: Shell_NotifyIconW(NIM_DELETE, &g_nid); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

/* ---------- second launch ---------- */
static int token_elevated(HANDLE proc) {  /* 1 / 0, -1 = can't tell */
    HANDLE t; TOKEN_ELEVATION e; DWORD n = 0; int r = -1;
    if (OpenProcessToken(proc, TOKEN_QUERY, &t)) {
        if (GetTokenInformation(t, TokenElevation, &e, sizeof e, &n)) r = e.TokenIsElevated != 0;
        CloseHandle(t);
    }
    return r;
}
/* a copy running as administrator and one running without can't talk to each other (UIPI) and see the driver differently */
static int other_rights(HWND w) {
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    int mine = token_elevated(GetCurrentProcess()), theirs = -1;
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) { theirs = token_elevated(p); CloseHandle(p); }
    if (mine >= 0 && theirs >= 0) return mine != theirs;
    /* its token is closed to us: Windows also refuses our messages to it if it runs as administrator */
    DWORD_PTR v = 0; SetLastError(0);
    return !SendMessageTimeoutW(w, WM_GLIM_VERSION, 0, 0, SMTO_ABORTIFHUNG, 3000, &v) && GetLastError() == ERROR_ACCESS_DENIED;
}
static void tell_other_rights(void) { if (!g_startup && !g_quiet) MessageBoxW(NULL, T(S_OTHER_RIGHTS), L"Glim", MB_OK | MB_ICONWARNING); }
static int show_running(void) {  /* shows a running copy of this version or a newer one; 1 = shown */
    HWND w = FindWindowW(APP_CLASS, NULL); DWORD pid = 0; DWORD_PTR ver = 0;
    if (!w) return 0;
    GetWindowThreadProcessId(w, &pid);
    if (!SendMessageTimeoutW(w, WM_GLIM_VERSION, 0, 0, SMTO_ABORTIFHUNG, 3000, &ver)) return 0;
    if ((ULONGLONG)ver < APP_FILEVER) return 0;
    AllowSetForegroundWindow(pid); PostMessageW(w, WM_SHOWME, 0, 0);
    return 1;
}
/* another Glim holds the single-instance mutex: show its window, or replace it if it's an older version.
   Returns 1 if this process now owns the mutex and should start. */
static int take_over(HANDLE mtx) {
    HWND other = FindWindowW(APP_CLASS, NULL);
    if (g_startup || !other) return 0;
    DWORD pid = 0; GetWindowThreadProcessId(other, &pid);
    if (other_rights(other)) { tell_other_rights(); return 0; }
    DWORD_PTR ver = 0;
    if (!SendMessageTimeoutW(other, WM_GLIM_VERSION, 0, 0, SMTO_ABORTIFHUNG, 3000, &ver)) return 0;  /* hung or not reachable */
    if ((ULONGLONG)ver >= APP_FILEVER) { AllowSetForegroundWindow(pid); PostMessageW(other, WM_SHOWME, 0, 0); return 0; }
    klog(L"an older Glim is running (pid %lu) - replacing it with %ls", pid, APP_VERSION);
    HANDLE proc = OpenProcess(SYNCHRONIZE, FALSE, pid);
    PostMessageW(other, WM_COMMAND, ID_TRAY_EXIT, 0);
    if (proc) { WaitForSingleObject(proc, 10000); CloseHandle(proc); }
    DWORD w = WaitForSingleObject(mtx, 5000);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) { g_tookOver = 1; return 1; }
    if (show_running()) return 0;  /* launched twice: the other new copy took over first */
    klog(L"the older Glim didn't exit");
    MessageBoxW(NULL, T(S_TAKEOVER_FAIL), L"Glim", MB_OK | MB_ICONWARNING);
    return 0;
}
/* "/selftest" while Glim is running: run it there. Returns -1 to run it here (no running copy, or not reachable) */
static int remote_self_test(void) {
    HWND other = FindWindowW(APP_CLASS, NULL); DWORD_PTR res = 0; DWORD pid = 0;
    if (!other) return -1;
    GetWindowThreadProcessId(other, &pid);
    if (other_rights(other)) { tell_other_rights(); return 1; }  /* its poll would take our test for Fn+Space */
    AllowSetForegroundWindow(pid);  /* its result box comes to the front */
    SetLastError(0);
    if (SendMessageTimeoutW(other, WM_GLIM_SELFTEST, (WPARAM)g_quiet, 0, SMTO_ABORTIFHUNG, g_quiet ? 60000 : 600000, &res))
        return res ? (res == 1 ? 0 : 1) : -1;
    return GetLastError() == ERROR_TIMEOUT ? 1 : -1;  /* still busy with it: don't run a second test here */
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR cmd, int show) {
    (void)prev; (void)show;
    g_inst = inst;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);  /* also for DLLs Windows components load later */
    load_dlls();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);  /* ShellExecute may use COM */
    g_startup = cmd && wcsstr(cmd, L"/startup") != NULL;
    g_quiet = cmd && wcsstr(cmd, L"/quiet") != NULL;
    { wchar_t ev[32]; if (GetEnvironmentVariableW(L"GLIM_TIMESCALE", ev, 32)) { g_scale = _wtoi(ev); if (g_scale < 1) g_scale = 1; } }
    GetEnvironmentVariableW(L"GLIM_FAKE", g_fake, MAX_PATH);
    GetEnvironmentVariableW(L"GLIM_FAKE_DRV", g_fakeDrv, 16);
#ifdef GLIM_TEST_BUILD
    { wchar_t ev[8]; g_updTest = g_fake[0] && GetEnvironmentVariableW(L"GLIM_UPDATE_AUTO", ev, 8); }
#endif
    init_dir();
    load_settings();
    if (cmd && wcsstr(cmd, L"/selftest"))      { int r = remote_self_test(); return r >= 0 ? r : (self_test() ? 1 : 0); }
    if (cmd && wcsstr(cmd, L"/autostart on"))  { wchar_t p[MAX_PATH], self[MAX_PATH]; set_autostart(1); self_path(self);
                                                 return run_target(p) && !_wcsicmp(p, self) ? 0 : 1; }
    if (cmd && wcsstr(cmd, L"/autostart off")) { set_autostart(0); return 0; }

    HANDLE mtx = CreateMutexW(NULL, TRUE, L"Local\\Glim_SingleInstance");
    if (!mtx && GetLastError() == ERROR_ACCESS_DENIED) { tell_other_rights(); return 0; }  /* a copy runs as administrator */
    if (mtx && GetLastError() == ERROR_ALREADY_EXISTS && !take_over(mtx)) { CloseHandle(mtx); return 0; }
    SetProcessDPIAware();  /* only if the manifest's per-monitor (v2) / system awareness didn't apply */
    HDC dc = GetDC(NULL); g_dpi = g_sysDpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(NULL, dc);
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);
    g_smallIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    if (!g_smallIcon) g_smallIcon = LoadIconW(NULL, IDI_APPLICATION);
    for (int i = 0; i < 4; i++)  /* the taskbar is on the primary monitor: its small icon size */
        g_trayIcon[i] = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(2 + i), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    g_darkBg = CreateSolidBrush(DARK_BG); g_darkList = CreateSolidBrush(DARK_LIST);

    WNDCLASSEXW wc = { sizeof wc };
    wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = APP_CLASS;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); wc.hIconSm = g_smallIcon;
    RegisterClassExW(&wc);

    RECT r = { 0, 0, S(344), S(360) };  /* layout() sets the final height */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&r, style, FALSE);
    g_wnd = CreateWindowExW(0, APP_CLASS, L"Glim", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (p_GetDpiForWindow) { UINT d = p_GetDpiForWindow(g_wnd); if (d) g_dpi = (int)d; }  /* its monitor's scale */
    make_fonts();
    set_window_icons();
    g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
    add_tray();
    build_ui();
    if ((g_dark = dark_wanted()) != 0) apply_theme();
    RegisterPowerSettingNotification(g_wnd, &MY_GUID_CONSOLE_DISPLAY_STATE, DEVICE_NOTIFY_WINDOW_HANDLE);
    heal_autostart();
    klog(L"Glim %ls started%ls, mode=%ls, onwake=%lu, respect_manual=%lu, windows build %lu, dpi %d, dark %d", APP_VERSION,
         g_startup ? L" (autostart)" : L"", LOG_MODE[g_mode], g_onwake, g_respect, g_build, g_dpi, g_dark);

    if (g_startup) {
        begin_grace(ms(90000));  /* Lenovo apps may reset the backlight after login */
        g_startRetrying = 1;
        apply_saved(L"startup");
        SetTimer(g_wnd, T_START1, ms(5000), NULL);
        SetTimer(g_wnd, T_START2, ms(20000), NULL);
        SetTimer(g_wnd, T_START3, ms(60000), NULL);
        update_status();
    } else {
        g_lastSet = g_lastSeen = backlight(-1);
        if (g_tookOver && g_respect && g_lastSet >= 0 && g_lastSet != (int)g_mode) {
            g_override = g_lastSet;  /* the replaced copy kept this level: most likely the user's Fn+Space */
            klog(L"keeping the manual mode of the replaced copy: %ls", LOG_MODE[g_override]);
            update_status();
        }
        show_window();
        if (!g_fake[0]) check_driver_dialog();
    }
    SetTimer(g_wnd, T_POLL, ms(2000) < 200 ? 200 : ms(2000), NULL);  /* detects Fn+Space changes */

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    ReleaseMutex(mtx); CloseHandle(mtx);
    return 0;
}
