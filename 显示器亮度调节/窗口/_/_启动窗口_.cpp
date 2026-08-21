#include "../_启动窗口.h"
// 依赖窗口类的自动生成头文件；本次托盘状态成员变更后重新编译本文件。

void __启动窗口::载入(窗口* 父窗, bool 模态)
{
	if (窗口句柄) return;

	// 弹出面板的尺寸由 Sciter 内容决定；外框不应参与系统飞出层定位。
	// 对齐系统音量飞出层：在当前 125% DPI 下约为 450×125 物理像素。
	窗口::参数 cs{ 0, 0, 360, 100 };
	cs.边框 = 窗口边框::无边框;
	cs.在任务栏显示 = false;
	cs.总在最前 = true;
	cs.控制按钮 = false;
	cs.Esc键关闭 = true;

	窗口::创建(cs);

	this->完毕(模态);
}

void __启动窗口::完毕(bool 模态)
{
	SciterUI::参数 cs;
#ifdef _DEBUG
	cs.文件_html = "index.html";
#else
	cs.内存_zip = R::htmZip;
#endif

	st.创建(cs, this);
	窗口::完毕(模态);
}
