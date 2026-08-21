#include <BEWin32UI/runtime.h>
#include <SciterUI/SciterUI.h>

namespace {
wchar_t 显示已有实例消息名称[] = L"MonitorBrightnessAdjuster.ShowExisting.9F7E0FC5";
wchar_t 主窗口标题[] = L"显示器亮度调节";

void 通知已有实例显示窗口()
{
	const UINT message = RegisterWindowMessageW(显示已有实例消息名称);
	// 只把显示请求投递到旧实例的消息队列，新进程无需等待窗口动画结束。
	const HWND previousWindow = FindWindowW(nullptr, 主窗口标题);
	if (previousWindow) PostMessageW(previousWindow, message, 0, 0);
}
}

int main(int nCShow = SW_SHOWNORMAL, char** argVec = nullptr)
{
	HANDLE instanceMutex = CreateMutexW(nullptr, FALSE, 显示已有实例消息名称);
	if (instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
		通知已有实例显示窗口();
		CloseHandle(instanceMutex);
		return 0;
	}

	全局初始化配置(GetModuleHandle(0), true);
	SciterUI::全局初始化();

	_启动窗口.初显(nCShow).载入();
	CloseHandle(instanceMutex);
	return Win32消息循环();
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPTSTR, int nCShow)
{
	return main(nCShow);
}
