#pragma once
#include <BEMod.h>
#include <windows.h>
#include <dwmapi.h>
#include <physicalmonitorenumerationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <wbemidl.h>
#include <comdef.h>
#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <devpkey.h>
#include <stdio.h>
#include "../SciterDWM.h"
#include "../resource.h"

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "wbemuuid.lib")
#pragma comment(lib, "setupapi.lib")

struct __启动窗口 : 窗口
{
	void 事件_创建完毕();

	void 事件_托盘(int 操作类型);

	void 事件_菜单项被单击(int 菜单ID);

	void 事件_被销毁();

	LRESULT 挂接消息(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);





	struct MonitorItem {
		StrW name;
		StrA channel; // "DDC" or "WMI"
		int index = 0;
		int brightness = 50;
		int displayNumber = 0;
		StrW instanceName;
		Arraybe<byte> levels;
	};

	SciterDom dom;
	Arraybe<MonitorItem> m_monitors;
	// 与原版 LibDisplayDataChannel 一致：物理监视器结构和可用句柄在整次扫描期间持久保存。
	// 不复制临时数组，也不在枚举回调结束后提前释放。
	PHYSICAL_MONITOR* m_allPhysicalMonitors = NULL;
	DWORD m_allPhysicalCount = 0;
	HANDLE* m_ddcHandles = NULL;
	DWORD m_ddcCount = 0;
	HWND m_gammaDesktop = NULL;
	HDC m_gammaDC = NULL;
	WORD m_originalGamma[3][256] = {};
	bool m_gammaCaptured = false;
	int m_gdiBrightness = 100;
	bool m_autoHideOnDeactivate = false;
	bool m_trayPopupVisible = false;
	static constexpr UINT_PTR 延迟失焦隐藏定时器 = 0x4D42;
	static constexpr const char* 配置文件 = "config.ini";

	static StrW 规范化显示器实例名(const wchar_t* value);

	static int 查询Windows显示器标识序号(const wchar_t* deviceInstance);

	StrW 查询显示配置友好名称(const wchar_t* gdiDevice, const wchar_t* wmiInstance,
		StrW* matchedDeviceInstance = nullptr);

	StrW 查询Wmi显示器友好名称(IWbemServices* service, const wchar_t* instanceName);

	void 清理DDC显示器();

	struct DdcEnumContext {
		__启动窗口* window;
		bool countOnly;
	};

	static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data);

	void 枚举DDC显示器(); 

	void 枚举WMI显示器();

	bool 应用GDI亮度(int percent, bool saveConfig);

	void 初始化GDI调光();

	void 还原并释放GDI调光();

	void 扫描所有显示器();

	int 读取上次选择();

	void 保存用户选择(int index);

	bool 设置Wmi亮度(c_StrW instanceName, int percent, const Arraybe<byte>& levels);

	int 设置显示器亮度(c_StrA channel, int index, int percent);

	void 调整窗口至托盘上方(int customHeight = 0);

	void 启动显示动画();

	void 设置托盘弹窗可见(bool visible);

	void 显示并激活弹窗(bool activate = true);

	bool 鼠标位于任务栏();

	SciterObj 获取系统主题颜色();

#pragma region 组件成员
	struct _st : SciterUI {
	} st;
	子菜单 托盘菜单;
#pragma endregion
	void 载入(窗口* 父窗 = 0, bool 模态 = 0);
	void 完毕(bool 模态 = 0);
}; extern __启动窗口 _启动窗口;
