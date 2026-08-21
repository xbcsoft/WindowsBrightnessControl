(function () {
    // 状态定义
    let monitors = [];
    let currentIndex = 0;
    let isExpanded = false;
    let isDragging = false;
    let currentBrightness = 50;
    let brightnessWriteTimer = null;
    let pendingBrightnessWrite = null;

    // DOM 元素
    const flyout = document.getElementById("flyout");
    const header = document.getElementById("header");
    const headerTitle = document.getElementById("header-title");
    const deviceListContainer = document.getElementById("device-list-container");
    const deviceList = document.getElementById("device-list");
    const sliderBox = document.getElementById("slider-box");
    const sliderFill = document.getElementById("slider-fill");
    const sliderThumb = document.getElementById("slider-thumb");
    const brightnessLabel = document.getElementById("brightness-label");

    // 格式化设备显示标题
    function getDeviceTitle(m) {
        if (!m) return "未检测到可用显示器";
        let typeStr = m.channel === "WMI" ? "WMI 调光" : (m.channel === "GDI" ? "GDI 软件调光" : "DDC 调光");
        let identity = m.displayNumber > 0 ? "显示器" + m.displayNumber + ": " + m.name : m.name;
        return identity + " (" + typeStr + ")";
    }

    // GDI Gamma 的安全实际范围是 50~100，对用户统一显示成 0~100。
    function toDisplayBrightness(monitor, actualValue) {
        if (monitor && monitor.channel === "GDI") {
            return Math.round((actualValue - 50) * 2);
        }
        return Math.round(actualValue);
    }

    // 更新界面上的滑块和数值
    function renderSlider(val) {
        const monitor = monitors[currentIndex];
        const minValue = monitor && monitor.channel === "GDI" ? 50 : 0;
        val = Math.max(minValue, Math.min(100, Math.round(val)));
        currentBrightness = val;
        const visualPercent = (val - minValue) * 100 / (100 - minValue);
        sliderFill.style.width = visualPercent + "%";
        sliderThumb.style.left = visualPercent + "%";
        brightnessLabel.innerText = toDisplayBrightness(monitor, val).toString();
    }

    // 拖动中的视觉位置必须先更新，不能等待同步 DDC 调用返回。
    function previewBrightness(val) {
        renderSlider(val);
        const itemVal = document.getElementById("item-val-" + currentIndex);
        if (itemVal) itemVal.innerText = toDisplayBrightness(monitors[currentIndex], currentBrightness) + "%";
    }

    // DDC 是同步原生调用；把它移出鼠标事件，让 Sciter 能先绘制拖动位置。
    function commitPendingBrightness() {
        brightnessWriteTimer = null;
        const request = pendingBrightnessWrite;
        pendingBrightnessWrite = null;
        if (!request) return;

        let actualVal = request.value;
        if (window.setBrightness) {
            try {
                const response = window.setBrightness({
                    channel: request.monitor.channel,
                    index: request.monitor.index,
                    val: request.value
                });
                if (typeof response !== "number" || response < 0) {
                    loadMonitors();
                    return;
                }
                actualVal = response;
            } catch (e) {
                console.log("setBrightness error:", e);
                loadMonitors();
                return;
            }
        }

        request.monitor.brightness = actualVal;
        // 不让较早的 DDC 回读覆盖较新的拖动预览。
        if (currentIndex === request.index && !pendingBrightnessWrite) {
            previewBrightness(actualVal);
        }
    }

    // 完全沿用“调整透明度”案例的调度：UI 立即更新、16ms 合并写入、松手立即提交。
    function applyBrightness(val, immediate) {
        if (monitors.length === 0 || !monitors[currentIndex]) return;
        const minValue = monitors[currentIndex].channel === "GDI" ? 50 : 0;
        val = Math.max(minValue, Math.min(100, Math.round(val)));
        previewBrightness(val);
        pendingBrightnessWrite = { monitor: monitors[currentIndex], index: currentIndex, value: val };

        if (immediate) {
            if (brightnessWriteTimer) {
                clearTimeout(brightnessWriteTimer);
                brightnessWriteTimer = null;
            }
            commitPendingBrightness();
        } else if (!brightnessWriteTimer) {
            brightnessWriteTimer = setTimeout(commitPendingBrightness, 16);
        }
    }

    // 渲染设备列表
    function renderDeviceList() {
        deviceList.innerHTML = "";
        if (monitors.length === 0) {
			isExpanded = false;
			flyout.classList.remove("expanded");
            flyout.classList.add("no-devices");
            headerTitle.innerText = "未找到支持亮度调节的显示器";
            if (window.setWindowHeight) window.setWindowHeight({ height: 40 });
            return;
        }
		flyout.classList.remove("no-devices");
		if (window.setWindowHeight) window.setWindowHeight({ height: isExpanded ? 228 : 100 });

        monitors.forEach((m, idx) => {
            const item = document.createElement("div");
            item.className = "device-item" + (idx === currentIndex ? " selected" : "");
            item.id = "device-item-" + idx;

            const iconSvg = `
                <svg class="device-icon" viewBox="0 0 24 24">
                    <rect x="2" y="3" width="20" height="14" rx="2" ry="2"></rect>
                    <line x1="8" y1="21" x2="16" y2="21"></line>
                    <line x1="12" y1="17" x2="12" y2="21"></line>
                </svg>
            `;

            const infoDiv = `
                <div class="device-info">
                    <div class="device-name">${m.displayNumber > 0 ? `显示器${m.displayNumber}: ${m.name}` : m.name}</div>
                    <div class="device-type">${m.channel === "WMI" ? "内置屏幕 (WMI 协议)" : (m.channel === "GDI" ? "系统全局 (GDI Gamma)" : "外接显示器 (DDC 协议)")}</div>
                </div>
            `;

            const valDiv = `<div class="device-val" id="item-val-${idx}">${toDisplayBrightness(m, m.brightness)}%</div>`;

            item.innerHTML = iconSvg + infoDiv + valDiv;

            item.addEventListener("click", () => {
                selectDevice(idx);
            });

            deviceList.appendChild(item);
        });

        // 更新顶部标题
        if (monitors[currentIndex]) {
            headerTitle.innerText = isExpanded ? "选择显示器" : getDeviceTitle(monitors[currentIndex]);
            renderSlider(monitors[currentIndex].brightness);
        }
    }

    // 选中某个设备
    function selectDevice(idx) {
        if (idx < 0 || idx >= monitors.length) return;
        currentIndex = idx;
		if (window.saveSelectedMonitor) {
			window.saveSelectedMonitor(currentIndex);
		}
        const items = document.querySelectorAll(".device-item");
        items.forEach((it, i) => {
            if (i === idx) it.classList.add("selected");
            else it.classList.remove("selected");
        });

        const cur = monitors[currentIndex];
        renderSlider(cur.brightness);
        headerTitle.innerText = isExpanded ? "选择显示器" : getDeviceTitle(cur);

        // 切换后收起
        toggleExpand(false);
    }

    // 展开 / 收起切换
    function toggleExpand(expand) {
        if (typeof expand === "boolean") {
            isExpanded = expand;
        } else {
            isExpanded = !isExpanded;
        }

        if (isExpanded) {
            flyout.classList.add("expanded");
            headerTitle.innerText = "选择显示器";
        } else {
            flyout.classList.remove("expanded");
            if (monitors[currentIndex]) {
                headerTitle.innerText = getDeviceTitle(monitors[currentIndex]);
            }
        }

        // 通知 C++ 调整窗口高度
		const targetHeight = isExpanded ? 228 : 100;
        if (window.setWindowHeight) {
            try {
                window.setWindowHeight({ height: targetHeight });
            } catch (e) {
                console.log("setWindowHeight error:", e);
            }
        }
    }

    // 顶部标题点击事件
    header.addEventListener("click", () => {
		if (monitors.length === 0) return;
        toggleExpand();
    });

    // 滑块计算逻辑
    function calcSliderValue(clientX) {
        const rect = sliderBox.getBoundingClientRect();
        const offsetX = clientX - rect.left;
        const ratio = Math.max(0, Math.min(1, offsetX / rect.width));
        const minValue = monitors[currentIndex] && monitors[currentIndex].channel === "GDI" ? 50 : 0;
        return Math.round(minValue + ratio * (100 - minValue));
    }

    // 滑块鼠标事件
    sliderBox.addEventListener("mousedown", (e) => {
        isDragging = true;
		applyBrightness(calcSliderValue(e.clientX), false);
        if (sliderBox.state && sliderBox.state.capture) {
            sliderBox.state.capture(true);
        }
		e.preventDefault();
    });

    // 与官方综合展示示例一致：捕获鼠标后，持续从滑块自身接收移动和松开事件。
    sliderBox.addEventListener("mousemove", (e) => {
        if (!isDragging) return;
        applyBrightness(calcSliderValue(e.clientX), false);
		e.preventDefault();
    });

    sliderBox.addEventListener("mouseup", (e) => {
        if (!isDragging) return;
        isDragging = false;
        if (sliderBox.state && sliderBox.state.capture) {
            sliderBox.state.capture(false);
        }
		applyBrightness(calcSliderValue(e.clientX), true);
		e.preventDefault();
    });

    // 滚轮调节亮度
    flyout.addEventListener("mousewheel", (e) => {
        const displayDelta = e.deltaY < 0 ? 3 : -3;
        const delta = monitors[currentIndex] && monitors[currentIndex].channel === "GDI"
            ? (displayDelta > 0 ? 1 : -1)
            : displayDelta;
        applyBrightness(currentBrightness + delta, true);
        e.preventDefault();
    });

    // 让主背景、交互状态和滑块统一使用 C++ 返回的系统强调色。
    function applyThemeColor(theme) {
        if (!theme || !theme.accent) {
            console.log("[Theme][JS] invalid theme object:", theme);
            return;
        }
        console.log("[Theme][JS] received accent=" + theme.accent
            + " surface=" + theme.surface + " control=" + theme.control);
        document.documentElement.style.setProperty("--accent-color", theme.accent);
        if (theme.surface) document.documentElement.style.setProperty("--accent-surface", theme.surface);
        if (theme.hover) document.documentElement.style.setProperty("--accent-hover", theme.hover);
        if (theme.active) document.documentElement.style.setProperty("--accent-active", theme.active);
        if (theme.selected) document.documentElement.style.setProperty("--accent-selected", theme.selected);
        const control = theme.control || theme.accent;
        document.documentElement.style.setProperty("--track-fill", control);
        document.documentElement.style.setProperty("--thumb-bg", control);

        // 轨道直接赋值；滑块使用 CSS 变量，才能响应悬停时的白色状态。
        if (theme.surface) flyout.style.backgroundColor = theme.surface;
        sliderFill.style.backgroundColor = control;
        console.log("[Theme][JS] applied flyout=" + flyout.style.backgroundColor
            + " slider=" + sliderFill.style.backgroundColor);
    }

    // 从 C++ 加载主题色
    function loadThemeColor() {
        if (window.getSystemThemeColor) {
            try {
                const theme = window.getSystemThemeColor();
                applyThemeColor(theme);
            } catch (e) {
                console.log("loadThemeColor error:", e);
            }
        } else {
            console.log("[Theme][JS] getSystemThemeColor is not injected yet");
        }
    }

    // 从 C++ 加载显示器列表
    function loadMonitors() {
        if (window.getMonitors) {
            try {
                const list = window.getMonitors();
                if (Array.isArray(list)) {
                    monitors = list;
					currentIndex = 0;
					if (window.getStartupSelection) {
						try {
							const savedIndex = Number(window.getStartupSelection());
							if (savedIndex >= 0 && savedIndex < monitors.length) currentIndex = savedIndex;
						} catch (e) {
							console.log("getStartupSelection error:", e);
						}
					}
                    renderDeviceList();
                    return;
                }
            } catch (e) {
                console.log("loadMonitors error:", e);
            }
        }

        // 接口尚未注入或枚举失败时保持空列表，绝不展示虚构硬件。
        monitors = [];
        currentIndex = 0;
        renderDeviceList();
    }

    // 初始化
    loadThemeColor();
    loadMonitors();

    // 导出全局刷新接口供 C++ 调用
    window.refreshData = function () {
        loadThemeColor();
        loadMonitors();
    };

    window.updateTheme = function (themeObj) {
        applyThemeColor(themeObj);
    };
})();
