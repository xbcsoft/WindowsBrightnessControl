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
	void 事件_创建完毕()
	{
		st.支持DWM动效(false);
		标题_(L"显示器亮度调节");
		初始化GDI调光();

		// 注册托盘图标
		托盘菜单.创建();
		托盘菜单.添加项(10001, L"退出");
		置托盘图标(IDI_TRAY_BRIGHTNESS, L"显示器亮度调节");

		// 获取 Sciter DOM 模型
		dom = st.取文档模型();

		// 注入 JS 接口: 获取系统主题色
		dom.注入JS函数("getSystemThemeColor", [this](SciterObj& arg) -> SciterObj {
			return 获取系统主题颜色();
		});

		// 注入 JS 接口: 获取显示器列表
		dom.注入JS函数("getMonitors", [this](SciterObj& arg) -> SciterObj {
			扫描所有显示器();
			SciterObj arr = dom.执行JS脚本("[]");
			for (int i = 0; i < m_monitors.count; ++i) {
				SciterObj item = dom.执行JS脚本("({})");
				item.置属性("name", WtoU8(m_monitors[i].name));
				item.置属性("displayNumber", m_monitors[i].displayNumber);
				item.置属性("channel", (char*)m_monitors[i].channel);
				item.置属性("index", m_monitors[i].index);
				item.置属性("brightness", m_monitors[i].brightness);
				arr.加入成员(item);
			}
			return arr;
		});

		dom.注入JS函数("getStartupSelection", [this](SciterObj& arg) -> SciterObj {
			return SciterObj(读取上次选择());
		});

		// 注入 JS 接口: 设置亮度
		dom.注入JS函数("setBrightness", [this](SciterObj& arg) -> SciterObj {
			StrA channel = (StrU8)arg.取属性("channel");
			int index = (int)arg.取属性("index");
			int value = (int)arg.取属性("val");
			return SciterObj(设置显示器亮度(channel, index, value));
		});

		dom.注入JS函数("saveSelectedMonitor", [this](SciterObj& arg) -> SciterObj {
			保存用户选择((int)arg);
			return SciterObj(true);
		});

		// 注入 JS 接口: 调整窗口高度
		dom.注入JS函数("setWindowHeight", [this](SciterObj& arg) -> SciterObj {
			int h = (int)arg.取属性("height");
			if (h > 0) {
				调整窗口至托盘上方(h);
			}
			return SciterObj(true);
		});

		// 保持原来的首次显示与动画链路，不在展开动画内部激活窗口。
		m_autoHideOnDeactivate = false;
		if (dom.ctx) {
			dom.调用JS函数("refreshData");
		}
		调整窗口至托盘上方();
		设置托盘弹窗可见(true);

		// 动画及本轮 DWM 合成完成后，只在下一次消息循环中激活一次。
		// 后续双击 exe 仍走显示并激活弹窗(false)，不会强制抢焦点。
		推迟调用子程序(1, [this]() {
			if (!窗口句柄 || !m_trayPopupVisible) return;
			激活(1);
			m_autoHideOnDeactivate = true;
		});
	}

	void 事件_托盘(int 操作类型)
	{
		if (操作类型 == 3) {
			弹出菜单(托盘菜单);
			return;
		}
		if (操作类型 == 1) {
			if (m_trayPopupVisible) {
				设置托盘弹窗可见(false);
			} else {
				显示并激活弹窗();
			}
		}
	}

	void 事件_菜单项被单击(int 菜单ID)
	{
		if (菜单ID == 10001) {
			销毁();
		}
	}

	void 事件_被销毁()
	{
		还原并释放GDI调光();
		清理DDC显示器();
		置托盘图标(Bytes());
	}

	LRESULT 挂接消息(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
	{
		if (msg == RegisterWindowMessageW(L"MonitorBrightnessAdjuster.ShowExisting.9F7E0FC5")) {
			// 第二个进程会在投递消息后立即退出；此处不抢焦点，避免随后的失焦把窗口重新隐藏。
			显示并激活弹窗(false);
			return 0;
		}

		switch (msg)
		{
		case WM_ACTIVATE:
			// 点击托盘时，WM_ACTIVATE(WA_INACTIVE) 先于 WM_LBUTTONUP 到达。
			// 任务栏的普通区域和托盘图标都会先让窗口失活，不能永久忽略整个任务栏。
			// 对任务栏点击短暂延迟：若随后收到本程序托盘图标的松键通知，由托盘逻辑切换；
			// 若没有托盘通知（点的是任务栏其他位置），定时器负责隐藏。
			if (LOWORD(wParam) == WA_INACTIVE && m_autoHideOnDeactivate) {
				if (鼠标位于任务栏()) {
					SetTimer(窗口句柄, 延迟失焦隐藏定时器, 120, nullptr);
				} else {
					设置托盘弹窗可见(false);
				}
			} else if (LOWORD(wParam) != WA_INACTIVE) {
				KillTimer(窗口句柄, 延迟失焦隐藏定时器);
				if (m_trayPopupVisible) m_autoHideOnDeactivate = true;
			}
			break;
		case WM_TIMER:
			if (wParam == 延迟失焦隐藏定时器) {
				KillTimer(窗口句柄, 延迟失焦隐藏定时器);
				if (m_trayPopupVisible && GetForegroundWindow() != 窗口句柄) {
					设置托盘弹窗可见(false);
				}
				return 0;
			}
			break;
		case WM_DWMCOLORIZATIONCOLORCHANGED:
			if (dom.ctx) dom.调用JS函数("updateTheme", 获取系统主题颜色());
			break;
		case WM_SETTINGCHANGE:
			if (dom.ctx) dom.调用JS函数("updateTheme", 获取系统主题颜色());
			break;
		}
		return 窗口::挂接消息(hwnd, msg, wParam, lParam);
	}





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

	static StrW 规范化显示器实例名(const wchar_t* value)
	{
		if (!value) return StrW();
		StrW result(value);
		if (文本_对比左边(result, L"\\\\?\\")) {
			result = 取文本子串(result, 4, 取文本长度(result) - 4);
		}
		const ssize_t interfaceGuid = 寻找文本(result, L"#{");
		if (interfaceGuid >= 0) result = 取文本左边(result, (size_t)interfaceGuid);
		result = 子文本替换(result, L"#", L"\\");
		// WmiMonitorBrightness 的实例名比 PnP 实例 ID 多一个方法实例后缀“_0”。
		if (文本_对比右边(result, L"_0")) {
			result = 取文本左边(result, 取文本长度(result) - 2);
		}
		return result;
	}

	static int 查询Windows显示器标识序号(const wchar_t* deviceInstance)
	{
		const StrW wanted = 规范化显示器实例名(deviceInstance);
		if (!((const wchar_t*)wanted)[0]) return 0;

		struct PnpMonitor {
			StrW instanceId;
			FILETIME firstInstall = {};

			int 比较顺序(const PnpMonitor& other) const
			{
				const LONG timeOrder = CompareFileTime(&firstInstall, &other.firstInstall);
				if (timeOrder != 0) return timeOrder;
				return _wcsicmp((const wchar_t*)instanceId, (const wchar_t*)other.instanceId);
			}
			bool operator>(const PnpMonitor& other) const { return 比较顺序(other) > 0; }
			bool operator<(const PnpMonitor& other) const { return 比较顺序(other) < 0; }
		};
		Arraybe<PnpMonitor> monitors;
		HDEVINFO devices = SetupDiGetClassDevsW(&GUID_DEVCLASS_MONITOR, nullptr, nullptr, DIGCF_PRESENT);
		if (devices == INVALID_HANDLE_VALUE) return 0;

		for (DWORD index = 0;; ++index) {
			SP_DEVINFO_DATA device = {};
			device.cbSize = sizeof(device);
			if (!SetupDiEnumDeviceInfo(devices, index, &device)) break;

			wchar_t instanceId[512] = {};
			if (!SetupDiGetDeviceInstanceIdW(devices, &device, instanceId, ARRAYSIZE(instanceId), nullptr)) continue;

			FILETIME firstInstall = {};
			DEVPROPTYPE propertyType = 0;
			DWORD requiredSize = 0;
			if (!SetupDiGetDevicePropertyW(devices, &device, &DEVPKEY_Device_FirstInstallDate,
				&propertyType, reinterpret_cast<PBYTE>(&firstInstall), sizeof(firstInstall), &requiredSize, 0) ||
				propertyType != DEVPROP_TYPE_FILETIME) {
				propertyType = 0;
				firstInstall = {};
				SetupDiGetDevicePropertyW(devices, &device, &DEVPKEY_Device_InstallDate,
					&propertyType, reinterpret_cast<PBYTE>(&firstInstall), sizeof(firstInstall), &requiredSize, 0);
			}
			PnpMonitor monitor;
			monitor.instanceId = 规范化显示器实例名(instanceId);
			monitor.firstInstall = firstInstall;
			加入成员(monitors, monitor);
		}
		SetupDiDestroyDeviceInfoList(devices);

		数组_排序(monitors);
		for (int index = 0; index < 取数组成员数(monitors); ++index) {
			if (_wcsicmp((const wchar_t*)monitors[index].instanceId, (const wchar_t*)wanted) == 0) {
				return index + 1;
			}
		}
		return 0;
	}

	StrW 查询显示配置友好名称(const wchar_t* gdiDevice, const wchar_t* wmiInstance,
		StrW* matchedDeviceInstance = nullptr)
	{
		wchar_t modelId[128] = {};
		if (wmiInstance) {
			const wchar_t* first = wcschr(wmiInstance, L'\\');
			const wchar_t* second = first ? wcschr(first + 1, L'\\') : nullptr;
			if (first && second && second > first + 1) {
				size_t length = min((size_t)(second - first - 1), ARRAYSIZE(modelId) - 1);
				wcsncpy_s(modelId, first + 1, length);
			}
		}

		UINT32 pathCount = 0, modeCount = 0;
		if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS || !pathCount) {
			return StrW();
		}
		auto* paths = new DISPLAYCONFIG_PATH_INFO[pathCount];
		auto* modes = new DISPLAYCONFIG_MODE_INFO[modeCount];
		LONG status = QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths,
			&modeCount, modes, nullptr);
		StrW result;
		if (status == ERROR_SUCCESS) {
			for (UINT32 i = 0; i < pathCount; ++i) {
				bool matches = false;
				if (gdiDevice && *gdiDevice) {
					DISPLAYCONFIG_SOURCE_DEVICE_NAME source = {};
					source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
					source.header.size = sizeof(source);
					source.header.adapterId = paths[i].sourceInfo.adapterId;
					source.header.id = paths[i].sourceInfo.id;
					matches = DisplayConfigGetDeviceInfo(&source.header) == ERROR_SUCCESS &&
						_wcsicmp(source.viewGdiDeviceName, gdiDevice) == 0;
				}

				DISPLAYCONFIG_TARGET_DEVICE_NAME target = {};
				target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
				target.header.size = sizeof(target);
				target.header.adapterId = paths[i].targetInfo.adapterId;
				target.header.id = paths[i].targetInfo.id;
				if (DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
				if (!matches && modelId[0]) {
					matches = 寻找文本(StrW(target.monitorDevicePath), StrW(modelId), 0, true) >= 0;
				}
				if (matches && target.monitorFriendlyDeviceName[0]) {
					result = target.monitorFriendlyDeviceName;
					if (matchedDeviceInstance) {
						*matchedDeviceInstance = 规范化显示器实例名(target.monitorDevicePath);
					}
					break;
				}
			}
		}
		delete[] modes;
		delete[] paths;
		return result;
	}

	StrW 查询Wmi显示器友好名称(IWbemServices* service, const wchar_t* instanceName)
	{
		if (!service || !instanceName) return StrW();
		IEnumWbemClassObject* enumerator = nullptr;
		HRESULT hr = service->ExecQuery(bstr_t("WQL"),
			bstr_t("SELECT InstanceName, UserFriendlyName FROM WmiMonitorID WHERE Active = TRUE"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
		if (FAILED(hr) || !enumerator) return StrW();

		StrW result;
		IWbemClassObject* object = nullptr;
		ULONG returned = 0;
		while (enumerator->Next(WBEM_INFINITE, 1, &object, &returned) == WBEM_S_NO_ERROR && returned > 0) {
			VARIANT instance;
			VariantInit(&instance);
			object->Get(L"InstanceName", 0, &instance, nullptr, nullptr);
			const bool matches = instance.vt == VT_BSTR && instance.bstrVal &&
				_wcsicmp(instance.bstrVal, instanceName) == 0;
			VariantClear(&instance);

			if (matches) {
				VARIANT name;
				VariantInit(&name);
				object->Get(L"UserFriendlyName", 0, &name, nullptr, nullptr);
				const VARTYPE elementType = name.vt & VT_TYPEMASK;
				if ((name.vt & VT_ARRAY) && name.parray &&
					(elementType == VT_I4 || elementType == VT_UI4 ||
					 elementType == VT_I2 || elementType == VT_UI2 || elementType == VT_UI1)) {
					LONG lower = 0, upper = -1;
					SafeArrayGetLBound(name.parray, 1, &lower);
					SafeArrayGetUBound(name.parray, 1, &upper);
					wchar_t buffer[128] = {};
					const LONG count = min(upper - lower + 1, (LONG)ARRAYSIZE(buffer) - 1);
					for (LONG offset = 0; offset < count; ++offset) {
						LONG index = lower + offset;
						ULONG character = 0;
						HRESULT elementHr = E_FAIL;
						switch (elementType) {
						case VT_I4: { LONG value = 0; elementHr = SafeArrayGetElement(name.parray, &index, &value); character = (ULONG)value; break; }
						case VT_UI4: { ULONG value = 0; elementHr = SafeArrayGetElement(name.parray, &index, &value); character = value; break; }
						case VT_I2: { SHORT value = 0; elementHr = SafeArrayGetElement(name.parray, &index, &value); character = (ULONG)(USHORT)value; break; }
						case VT_UI2: { USHORT value = 0; elementHr = SafeArrayGetElement(name.parray, &index, &value); character = value; break; }
						case VT_UI1: { BYTE value = 0; elementHr = SafeArrayGetElement(name.parray, &index, &value); character = value; break; }
						}
						if (FAILED(elementHr) || character == 0) break;
						buffer[offset] = (wchar_t)character;
					}
					result = buffer;
				}
				VariantClear(&name);
			}
			object->Release();
			if (matches) break;
		}
		enumerator->Release();
		return result;
	}

	void 清理DDC显示器()
	{
		if (m_allPhysicalMonitors) {
			DestroyPhysicalMonitors(m_allPhysicalCount, m_allPhysicalMonitors);
			delete[] m_allPhysicalMonitors;
		}
		delete[] m_ddcHandles;
		m_allPhysicalMonitors = NULL;
		m_allPhysicalCount = 0;
		m_ddcHandles = NULL;
		m_ddcCount = 0;
	}

	struct DdcEnumContext {
		__启动窗口* window;
		bool countOnly;
	};

	static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC dc, LPRECT rect, LPARAM data)
	{
		DdcEnumContext* context = (DdcEnumContext*)data;
		__启动窗口* pThis = context->window;
		DWORD count = 0;
		if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count)) {
			return FALSE;
		}
		pThis->m_allPhysicalCount += count;
		if (context->countOnly || count == 0) return TRUE;

		PHYSICAL_MONITOR* physicals = pThis->m_allPhysicalMonitors + (pThis->m_allPhysicalCount - count);
		if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, physicals)) {
			return FALSE;
		}
		for (DWORD i = 0; i < count; ++i) {
			DWORD minB = 0, curB = 0, maxB = 0;
			if (GetMonitorBrightness(physicals[i].hPhysicalMonitor, &minB, &curB, &maxB)) {
				MonitorItem item;
				item.channel = "DDC";
				item.index = (int)pThis->m_ddcCount;
				item.brightness = maxB > minB ? (int)((curB - minB) * 100.0 / (maxB - minB) + 0.5) : (int)curB;
				MONITORINFOEXW monitorInfo = {};
				monitorInfo.cbSize = sizeof(monitorInfo);
				StrW friendlyName;
				StrW deviceInstance;
				if (GetMonitorInfoW(monitor, &monitorInfo)) {
					friendlyName = pThis->查询显示配置友好名称(monitorInfo.szDevice, nullptr, &deviceInstance);
				}
				StrW desc = physicals[i].szPhysicalMonitorDescription;
				item.name = friendlyName ? friendlyName :
					(desc ? desc : sprintF<W>(L"外接显示器 %d", pThis->m_ddcCount + 1));
				item.displayNumber = pThis->查询Windows显示器标识序号(deviceInstance);
				pThis->m_ddcHandles[pThis->m_ddcCount++] = physicals[i].hPhysicalMonitor;
				pThis->m_monitors.push(item);
			}
		}
		return TRUE;
	}

	void 枚举DDC显示器()
	{
		DdcEnumContext context{ this, true };
		if (!EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&context) || m_allPhysicalCount == 0) return;
		m_allPhysicalMonitors = new PHYSICAL_MONITOR[m_allPhysicalCount];
		m_ddcHandles = new HANDLE[m_allPhysicalCount];
		m_allPhysicalCount = 0;
		m_ddcCount = 0;
		context.countOnly = false;
		if (!EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&context)) 清理DDC显示器();
	} 

	void 枚举WMI显示器()
	{
		HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		IWbemLocator* pLoc = NULL;
		hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
		if (FAILED(hr) || !pLoc) return;

		IWbemServices* pSvc = NULL;
		hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
		if (FAILED(hr) || !pSvc) {
			pLoc->Release();
			return;
		}

		CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
			RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

		IEnumWbemClassObject* pEnum = NULL;
		hr = pSvc->ExecQuery(
			bstr_t("WQL"),
			bstr_t("SELECT * FROM WmiMonitorBrightness WHERE Active = TRUE"),
			WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
			NULL, &pEnum);

		if (SUCCEEDED(hr) && pEnum) {
			IWbemClassObject* pclsObj = NULL;
			ULONG uRet = 0;
			int wmiIdx = 1;
			while (pEnum->Next(WBEM_INFINITE, 1, &pclsObj, &uRet) == WBEM_S_NO_ERROR && uRet > 0) {
				VARIANT vtCur;
				VariantInit(&vtCur);
				pclsObj->Get(L"CurrentBrightness", 0, &vtCur, 0, 0);

				VARIANT vtLevel;
				VariantInit(&vtLevel);
				pclsObj->Get(L"Level", 0, &vtLevel, 0, 0);

				VARIANT vtInst;
				VariantInit(&vtInst);
				pclsObj->Get(L"InstanceName", 0, &vtInst, 0, 0);

				MonitorItem item;
				item.channel = "WMI";
				item.index = m_monitors.count;
				item.name = sprintF<W>(L"内置屏幕 %d", wmiIdx++);
				item.brightness = (vtCur.vt == VT_UI1) ? vtCur.bVal : 50;
				if (vtInst.vt == VT_BSTR && vtInst.bstrVal) {
					item.instanceName = vtInst.bstrVal;
					item.displayNumber = 查询Windows显示器标识序号(vtInst.bstrVal);
					StrW friendlyName = 查询Wmi显示器友好名称(pSvc, vtInst.bstrVal);
					if (!friendlyName) friendlyName = 查询显示配置友好名称(nullptr, vtInst.bstrVal);
					if (friendlyName) item.name = friendlyName;
				}

				if (vtLevel.vt == (VT_ARRAY | VT_UI1) && vtLevel.parray) {
					SAFEARRAY* sa = vtLevel.parray;
					LONG lBound = 0, uBound = 0;
					SafeArrayGetLBound(sa, 1, &lBound);
					SafeArrayGetUBound(sa, 1, &uBound);
					byte* pData = NULL;
					if (SUCCEEDED(SafeArrayAccessData(sa, (void**)&pData))) {
						for (LONG k = lBound; k <= uBound; ++k) {
							item.levels.push(pData[k - lBound]);
						}
						SafeArrayUnaccessData(sa);
					}
				}

				m_monitors.push(item);

				VariantClear(&vtCur);
				VariantClear(&vtLevel);
				VariantClear(&vtInst);
				pclsObj->Release();
			}
			pEnum->Release();
		}

		pSvc->Release();
		pLoc->Release();
	}

	bool 应用GDI亮度(int percent, bool saveConfig)
	{
		// Windows 的保护规则使线性 Gamma Ramp 的有效下限约为 50%。
		percent = max(50, min(100, percent));
		if (!m_gammaCaptured || !m_gammaDC) return false;

		WORD ramp[3][256] = {};
		for (int channel = 0; channel < 3; ++channel) {
			for (int value = 0; value < 256; ++value) {
				const unsigned int scaled =
					(unsigned int)m_originalGamma[channel][value] * (unsigned int)percent / 100U;
				ramp[channel][value] = (WORD)min(65535U, scaled);
			}
		}
		if (!SetDeviceGammaRamp(m_gammaDC, ramp)) return false;

		m_gdiBrightness = percent;
		if (saveConfig) {
			写配置项(配置文件, "Config", "GdiBrightness", sprintF("%d", percent));
		}
		return true;
	}

	void 初始化GDI调光()
	{
		m_gdiBrightness = 100;
		m_gammaDesktop = GetDesktopWindow();
		m_gammaDC = GetDC(m_gammaDesktop);
		m_gammaCaptured = m_gammaDC && GetDeviceGammaRamp(m_gammaDC, m_originalGamma);
		if (!m_gammaCaptured) return;

		int saved = atoi((char*)读配置项(配置文件, "Config", "GdiBrightness", "100"));
		saved = max(50, min(100, saved));
		if (saved < 100 && !应用GDI亮度(saved, false)) {
			m_gdiBrightness = 100;
		}
	}

	void 还原并释放GDI调光()
	{
		// 托盘“退出”直接调用窗口::销毁()，因此恢复动作必须允许在
		// 事件_被销毁() 中无条件执行，不能依赖可能不同步的 changed 标志。
		if (m_gammaCaptured && m_gammaDC) {
			SetDeviceGammaRamp(m_gammaDC, m_originalGamma);
		}
		if (m_gammaDC) ReleaseDC(m_gammaDesktop, m_gammaDC);
		m_gammaDC = NULL;
		m_gammaDesktop = NULL;
		m_gammaCaptured = false;
	}

	void 扫描所有显示器()
	{
		清理DDC显示器();
		m_monitors.clear();
		枚举DDC显示器();
		枚举WMI显示器();
		// GDI Gamma Ramp 是全局软件调光，不依赖显示器是否支持 DDC/WMI，始终提供。
		MonitorItem gdi;
		gdi.channel = "GDI";
		gdi.index = 0;
		gdi.name = L"GDI 软件调光";
		gdi.brightness = m_gdiBrightness;
		m_monitors.push(gdi);
	}

	int 读取上次选择()
	{
		StrA channel = (StrU8)读配置项(配置文件, "Config", "Channel", "");
		StrW name = (StrW)读配置项(配置文件, "Config", "Name", "");
		int index = atoi((char*)读配置项(配置文件, "Config", "Index", "0"));

		for (int i = 0; i < m_monitors.count; ++i) {
			if (m_monitors[i].channel == channel && m_monitors[i].name == name) return i;
		}
		for (int i = 0; i < m_monitors.count; ++i) {
			if (m_monitors[i].channel == channel && m_monitors[i].index == index) return i;
		}
		return 0;
	}

	void 保存用户选择(int index)
	{
		if (index < 0 || index >= m_monitors.count) return;
		const MonitorItem& item = m_monitors[index];
		写配置项(配置文件, "Config", "Channel", item.channel);
		写配置项(配置文件, "Config", "Name", item.name);
		写配置项(配置文件, "Config", "Index", sprintF("%d", item.index));
	}

	bool 设置Wmi亮度(c_StrW instanceName, int percent, const Arraybe<byte>& levels)
	{
		if (percent < 0) percent = 0;
		if (percent > 100) percent = 100;

		byte targetLevel = (byte)percent;
		if (levels.count > 0) {
			int minDiff = 999;
			for (int i = 0; i < levels.count; ++i) {
				int diff = abs((int)levels[i] - percent);
				if (diff < minDiff) {
					minDiff = diff;
					targetLevel = levels[i];
				}
			}
		}

		IWbemLocator* pLoc = NULL;
		HRESULT hr = CoCreateInstance(CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (LPVOID*)&pLoc);
		if (FAILED(hr) || !pLoc) return false;

		IWbemServices* pSvc = NULL;
		hr = pLoc->ConnectServer(_bstr_t(L"ROOT\\WMI"), NULL, NULL, 0, NULL, 0, 0, &pSvc);
		if (FAILED(hr) || !pSvc) {
			pLoc->Release();
			return false;
		}

		CoSetProxyBlanket(pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, NULL,
			RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE);

		bool success = false;
		IWbemClassObject* pClass = NULL;
		hr = pSvc->GetObject(bstr_t("WmiMonitorBrightnessMethods"), 0, NULL, &pClass, NULL);
		if (SUCCEEDED(hr) && pClass) {
			IWbemClassObject* pInParamsDef = NULL;
			hr = pClass->GetMethod(L"WmiSetBrightness", 0, &pInParamsDef, NULL);
			if (SUCCEEDED(hr) && pInParamsDef) {
				IWbemClassObject* pInParams = NULL;
				hr = pInParamsDef->SpawnInstance(0, &pInParams);
				if (SUCCEEDED(hr) && pInParams) {
					VARIANT varTimeout;
					VariantInit(&varTimeout);
					// WMI 的 CIM_UINT32 在 VARIANT 中由 VT_I4 承载；VT_UI4 会返回
					// WBEM_E_TYPE_MISMATCH，随后 ExecMethod 报参数无效。
					varTimeout.vt = VT_I4;
					varTimeout.lVal = 0; // 立即应用，避免连续拖动不断重启延时过渡。
					HRESULT putTimeoutHr = pInParams->Put(L"Timeout", 0, &varTimeout, CIM_UINT32);

					VARIANT varBrightness;
					VariantInit(&varBrightness);
					varBrightness.vt = VT_UI1;
					varBrightness.bVal = targetLevel;
					HRESULT putBrightnessHr = pInParams->Put(L"Brightness", 0, &varBrightness, CIM_UINT8);

					IEnumWbemClassObject* pEnum = NULL;
					if (SUCCEEDED(putTimeoutHr) && SUCCEEDED(putBrightnessHr)) {
						hr = pSvc->ExecQuery(bstr_t("WQL"),
							bstr_t("SELECT * FROM WmiMonitorBrightnessMethods WHERE Active = TRUE"),
							WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);
					} else {
						hr = FAILED(putTimeoutHr) ? putTimeoutHr : putBrightnessHr;
					}
					if (SUCCEEDED(hr) && pEnum) {
						IWbemClassObject* pInstObj = NULL;
						ULONG uRet = 0;
						while (pEnum->Next(WBEM_INFINITE, 1, &pInstObj, &uRet) == WBEM_S_NO_ERROR && uRet > 0) {
							VARIANT vtInstance;
							VariantInit(&vtInstance);
							pInstObj->Get(L"InstanceName", 0, &vtInstance, 0, 0);
							const bool isTarget = vtInstance.vt == VT_BSTR && vtInstance.bstrVal &&
								instanceName && _wcsicmp(vtInstance.bstrVal, (const wchar_t*)instanceName) == 0;
							VariantClear(&vtInstance);

							VARIANT vtPath;
							VariantInit(&vtPath);
							if (isTarget) pInstObj->Get(L"__PATH", 0, &vtPath, 0, 0);
							if (isTarget && vtPath.vt == VT_BSTR && vtPath.bstrVal) {
								IWbemClassObject* pOutParams = NULL;
								hr = pSvc->ExecMethod(vtPath.bstrVal, bstr_t("WmiSetBrightness"), 0,
									NULL, pInParams, &pOutParams, NULL);
								success = SUCCEEDED(hr);
								if (pOutParams) pOutParams->Release();
							}
							VariantClear(&vtPath);
							pInstObj->Release();
							if (isTarget) break;
						}
						pEnum->Release();
					}
					pInParams->Release();
				}
				pInParamsDef->Release();
			}
			pClass->Release();
		}

		pSvc->Release();
		pLoc->Release();
		return success;
	}

	int 设置显示器亮度(c_StrA channel, int index, int percent)
	{
		if (percent < 0) percent = 0;
		if (percent > 100) percent = 100;
		if (channel == "GDI") {
			return 应用GDI亮度(percent, true) ? m_gdiBrightness : -1;
		}

		if (channel == "DDC") {
			// 对齐原版 DdcAdjuster：用可用 DDC 句柄表按 DDC 索引读、写、再读。
			if (index < 0 || (DWORD)index >= m_ddcCount) return -1;
			DWORD minB = 0, currentB = 0, maxB = 0;
			HANDLE handle = m_ddcHandles[index];
			if (!GetMonitorBrightness(handle, &minB, &currentB, &maxB)) return -1;
			DWORD target = (DWORD)((maxB - minB) * percent / 100.0 + minB);
			if (!SetMonitorBrightness(handle, target)) return -1;
			if (!GetMonitorBrightness(handle, &minB, &currentB, &maxB)) return -1;
			int actual = maxB > minB ? (int)((currentB - minB) * 100.0 / (maxB - minB) + 0.5) : (int)currentB;
			for (int i = 0; i < m_monitors.count; ++i) {
				if (m_monitors[i].channel == "DDC" && m_monitors[i].index == index) {
					m_monitors[i].brightness = actual;
					break;
				}
			}
			return actual;
		}

		for (int i = 0; i < m_monitors.count; ++i) {
			if (m_monitors[i].channel == channel && m_monitors[i].index == index) {
				if (channel == "WMI") {
					if (设置Wmi亮度(m_monitors[i].instanceName, percent, m_monitors[i].levels)) {
						m_monitors[i].brightness = percent;
						return percent;
					}
					return -1;
				}
				return -1;
			}
		}
		return -1;
	}

	void 调整窗口至托盘上方(int customHeight = 0)
	{
		// 与系统音量弹窗一致：以任务栏本身为锚点，而非鼠标或工作区。
		HWND taskbar = FindWindowW(L"Shell_TrayWnd", NULL);
		RECT rcTaskbar{}, rcWindow{};
		GetWindowRect(窗口句柄, &rcWindow);
		if (taskbar && GetWindowRect(taskbar, &rcTaskbar)) {
			int width = rcWindow.right - rcWindow.left;
			int height = customHeight > 0 ? dpi(customHeight) : rcWindow.bottom - rcWindow.top;
			// 首次显示前系统可能尚未返回有效客户区尺寸，不能用 0×0 去重设窗口。
			if (width <= 0) width = dpi(360);
			if (height <= 0) height = dpi(customHeight > 0 ? customHeight : 100);
			int x = rcTaskbar.right - width;
			int y = rcTaskbar.top - height;
			SetWindowPos(窗口句柄, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
		}
	}

	void 启动显示动画()
	{
		SciterDWM背景特效(st, SciterDWMType::Acrylic, true);

		RECT rc{};
		GetWindowRect(窗口句柄, &rc);
		const int animationX = rc.left;
		const int animationBottom = rc.bottom;
		const int animationWidth = rc.right - rc.left;
		const int animationHeight = rc.bottom - rc.top;
		const int startHeight = max(1, dpi(8));
		SetWindowPos(窗口句柄, HWND_TOPMOST,
			animationX, animationBottom - startHeight,
			animationWidth, startHeight, SWP_NOACTIVATE);
		ShowWindow(窗口句柄, SW_SHOWNOACTIVATE);
		const ULONGLONG animationStart = GetTickCount64();

		// WM_TIMER 与桌面合成帧不同步，会产生 10~30ms 不等的帧间隔。
		// 直接用 DwmFlush 对齐每一次 DWM 提交，约 330ms 内完成稳定合成帧。
		for (;;) {
			const ULONGLONG elapsed = GetTickCount64() - animationStart;
			const double progress = min(1.0, elapsed / 330.0);

			// Windows Fluent 常用 cubic-bezier(0.1, 0.9, 0.2, 1)：
			// 起步很快，但最后保留一小段柔和贴合，而不是提前冲到终点后停住。
			auto bezier = [](double t, double p1, double p2) {
				const double u = 1.0 - t;
				return 3.0 * u * u * t * p1 + 3.0 * u * t * t * p2 + t * t * t;
			};
			auto bezierDerivative = [](double t, double p1, double p2) {
				const double u = 1.0 - t;
				return 3.0 * u * u * p1 + 6.0 * u * t * (p2 - p1) +
					3.0 * t * t * (1.0 - p2);
			};
			double curveT = progress;
			for (int i = 0; i < 5; ++i) {
				const double error = bezier(curveT, 0.1, 0.2) - progress;
				const double derivative = bezierDerivative(curveT, 0.1, 0.2);
				if (derivative < 0.0001) break;
				curveT = max(0.0, min(1.0, curveT - error / derivative));
			}
			const double eased = bezier(curveT, 0.9, 1.0);
			const int height = startHeight +
				(int)((animationHeight - startHeight) * eased + 0.5);

			SetWindowPos(窗口句柄, HWND_TOPMOST,
				animationX, animationBottom - height,
				animationWidth, height,
				SWP_NOACTIVATE | SWP_NOSENDCHANGING);
			RedrawWindow(窗口句柄, NULL, NULL,
				RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
			DwmFlush();

			if (progress >= 1.0) break;
		}
	}

	void 设置托盘弹窗可见(bool visible)
	{
		if (!visible) KillTimer(窗口句柄, 延迟失焦隐藏定时器);
		m_trayPopupVisible = visible;
		if (visible) {
			启动显示动画();
		} else {
			可视_(false);
			ShowWindow(窗口句柄, SW_HIDE);
		}
	}

	void 显示并激活弹窗(bool activate = true)
	{
		m_autoHideOnDeactivate = false;
		KillTimer(窗口句柄, 延迟失焦隐藏定时器);
		调整窗口至托盘上方();
		if (!m_trayPopupVisible) {
			设置托盘弹窗可见(true);
		}
		if (activate) {
			置前台();
			激活(1);
			m_autoHideOnDeactivate = true;
		}
		if (dom.ctx) {
			dom.调用JS函数("refreshData");
		}
	}

	bool 鼠标位于任务栏()
	{
		POINT pt = {};
		if (!GetCursorPos(&pt)) return false;

		HWND hit = WindowFromPoint(pt);
		HWND root = hit ? GetAncestor(hit, GA_ROOT) : NULL;
		if (!root) return false;

		charW className[64] = {};
		GetClassNameW(root, className, ARRAYSIZE(className));
		return lstrcmpW(className, L"Shell_TrayWnd") == 0 ||
			lstrcmpW(className, L"Shell_SecondaryTrayWnd") == 0;
	}

	SciterObj 获取系统主题颜色()
	{
		COLORREF color = RGB(0, 120, 215);

		// AccentColorMenu 的 DWORD 格式是 0xAABBGGRR，低字节依次为 R、G、B。
		DWORD accent = 0;
		DWORD size = sizeof(accent);
		const LSTATUS queryStatus = RegGetValueW(HKEY_CURRENT_USER,
			LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\Accent)",
			L"AccentColorMenu", RRF_RT_REG_DWORD, nullptr, &accent, &size);
		if (queryStatus == ERROR_SUCCESS) {
			color = RGB(accent & 0xFF, (accent >> 8) & 0xFF, (accent >> 16) & 0xFF);
		}

		const int r = GetRValue(color);
		const int g = GetGValue(color);
		const int b = GetBValue(color);
		const int lightR = min(255, r + (int)((255 - r) * 0.22));
		const int lightG = min(255, g + (int)((255 - g) * 0.22));
		const int lightB = min(255, b + (int)((255 - b) * 0.22));
		const int darkR = max(0, (int)(r * 0.70));
		const int darkG = max(0, (int)(g * 0.70));
		const int darkB = max(0, (int)(b * 0.70));

		SciterObj theme = dom.执行JS脚本("{}");
		theme.置属性("accent", (char*)sprintF("#%02X%02X%02X", r, g, b));
		theme.置属性("surface", (char*)sprintF("rgba(%d,%d,%d,0.70)", r, g, b));
		theme.置属性("hover", "rgba(255,255,255,0.12)");
		theme.置属性("active", "rgba(255,255,255,0.22)");
		theme.置属性("selected", (char*)sprintF("rgba(%d,%d,%d,0.38)", darkR, darkG, darkB));
		theme.置属性("control", (char*)sprintF("#%02X%02X%02X", lightR, lightG, lightB));
		return theme;
	}

#pragma region 组件成员
	struct _st : SciterUI {
	} st;
	子菜单 托盘菜单;
#pragma endregion
	void 载入(窗口* 父窗 = 0, bool 模态 = 0);
	void 完毕(bool 模态 = 0);
} _启动窗口;
