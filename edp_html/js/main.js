/**
 * 墨水屏控制台主模块
 * 原始版本功能完全保留，增量增加：
 * - 自动识别 Web 协议（6275...）和 APP 协议（0000ff01...）
 * - APP 模式下的图片发送（支持 LZ77 压缩、A/B 面选择）
 * - 不影响原有任何功能，新旧模式自动切换
 * - 新增：根据协议动态显示/隐藏功能界面
 * - 新增：同步时间按钮（无弹窗，仅 log）
 */

// ==================== 全局变量 ====================
let bleDevice, gattServer;
let epdService, epdCharacteristic, txCharacteristic, cmdCharacteristic;
let startTime, msgIndex, appVersion;
let canvas, ctx, textDecoder;
let paintManager, cropManager;

// APP版本号
const APP_VERSION = '2.1.0';
const APP_BUILD_DATE = '2026-03-31';

let ditherBrightness = 1.0;    // 亮度系数

// 蓝牙命令定义（与固件保持一致）
const EpdCmd = {
    SET_PINS: 0x00,
    INIT: 0x01,
    CLEAR: 0x02,
    SEND_CMD: 0x03,
    SEND_DATA: 0x04,
    REFRESH: 0x05,
    SLEEP: 0x06,
    SET_TIME: 0x20,
    SET_WEEK_START: 0x21,
    WRITE_IMG: 0x30,           // v1.6 普通传输
    WRITE_BLOCK: 0x31,         // CRC传输块
    QUERY_STATUS: 0x32,        // 查询传输状态
    RESET_TRANSFER: 0x33,      // 重置传输状态
    SET_CONFIG: 0x90,
    SYS_RESET: 0x91,
    SYS_SLEEP: 0x92,
    CFG_ERASE: 0x99,
    SET_HOLIDAYS: 0xB6,        // 设置假期数据
    GHOSTING_CLEAR: 0xB7,      // 开始残影消除
    GHOSTING_STOP: 0xB8        // 停止残影消除
};

// 画布尺寸预设
const canvasSizes = [
    { name: '1.54_152_152', width: 152, height: 152 },
    { name: '1.54_200_200', width: 200, height: 200 },
    { name: '2.13_104_212', width: 104, height: 212 },
    { name: '2.13_212_104', width: 212, height: 104 },
    { name: '2.13_250_122', width: 250, height: 122 },
    { name: '2.13_128_250', width: 128, height: 250 },
    { name: '2.66_152_296', width: 152, height: 296 },//52810单独的
    { name: '2.66_296_152', width: 296, height: 152 },
    { name: '2.8_152_296', width: 152, height: 296 },//四色2.8寸
    { name: '2.9_296_128', width: 296, height: 128 },
    { name: '2.9_128_296', width: 128, height: 296 },//盒马2.9寸
    { name: '2.9_384_168', width: 384, height: 168 },
    { name: '3.1_300_300', width: 304, height: 304 },
    { name: '3.5_384_184', width: 384, height: 184 },
    { name: '3.7_240_416', width: 240, height: 416 },//3.7寸 第一代AI智屏壳
    { name: '3.7_416_240', width: 416, height: 240 },
    { name: '3.97_800_480', width: 800, height: 480 },
    { name: '3.98_768_552', width: 768, height: 552 },//3.98寸四色手机壳
    { name: '4.2_400_300', width: 400, height: 300 },
    { name: '5.79_792_272', width: 792, height: 272 },
    { name: '5.81_720_256', width: 720, height: 256 },//海带屏
    { name: '5.83_600_448', width: 600, height: 448 },
    { name: '5.83_648_480', width: 648, height: 480 },
    { name: '7.4_800_480', width: 800, height: 480 },//SES7.4_GU140
    { name: '7.5_640_384', width: 640, height: 384 },
    { name: '7.5_800_480', width: 800, height: 480 },
    { name: '7.5_880_528', width: 880, height: 528 },
    { name: '10.2_960_640', width: 960, height: 640 },
    { name: '10.85_1360_480', width: 1360, height: 480 },
    { name: '11.6_960_640', width: 960, height: 640 },
    { name: '4E_600_400', width: 600, height: 400 },
    { name: '7.3E6', width: 480, height: 800 }, 
    { name: '7.3E6_800_480', width: 800, height: 480 }
];

// ==================== 双协议支持 ====================
// 在现有全局变量下方添加
let appMtuNegotiated = false;

let appModeEnabled = false;
let currentEpdIndex = 1;
let compressEnabled = false;
let epdTypeForApp = 0x06;
let imageDataA = null, imageDataB = null, currentImageDataForApp = null;
// APP 模式专用：存储从主画布同步过来的图像数据
let storedImageDataA = null;   // ImageData 对象
let storedImageDataB = null;

// ==================== 工具函数 ====================
function hex2bytes(hex) {
    const bytes = [];
    for (let i = 0; i < hex.length; i += 2) {
        bytes.push(parseInt(hex.substr(i, 2), 16));
    }
    return new Uint8Array(bytes);
}

function bytes2hex(data) {
    return new Uint8Array(data).reduce((memo, i) => memo + ("0" + i.toString(16)).slice(-2), "");
}

function intToHex(intIn) {
    let stringOut = ("0000" + intIn.toString(16)).substr(-4);
    return stringOut.substring(2, 4) + stringOut.substring(0, 2);
}

function resetVariables() {
    gattServer = null;
    epdService = null;
    epdCharacteristic = null;
    txCharacteristic = null;
    cmdCharacteristic = null;
    msgIndex = 0;
    const logEl = document.getElementById("log");
    if (logEl) logEl.innerHTML = '';
}

// ==================== 蓝牙写入（带防冲突锁）====================
let writeInProgress = false;
const WRITE_DELAY_MS = 50;
function sleep(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }

async function write(cmd, data, withResponse = true) {
    while (writeInProgress) await sleep(10);
    if (!epdCharacteristic) {
        addLog("服务不可用，请检查蓝牙连接");
        return false;
    }
    writeInProgress = true;
    try {
        const payload = [cmd];
        if (data) {
            if (typeof data === 'string') data = hex2bytes(data);
            if (data instanceof Uint8Array) data = Array.from(data);
            payload.push(...data);
        }
        addLog(bytes2hex(payload), '⇑');
        if (withResponse) {
            await epdCharacteristic.writeValueWithResponse(Uint8Array.from(payload));
        } else {
            await epdCharacteristic.writeValueWithoutResponse(Uint8Array.from(payload));
        }
        await sleep(WRITE_DELAY_MS);
        return true;
    } catch (e) {
        console.error(e);
        if (e.message) addLog("write: " + e.message);
        return false;
    } finally {
        writeInProgress = false;
    }
}

// ==================== 图像传输（支持CRC模式）====================
async function writeImage(data, step = 'bw') {
    const mtu = parseInt(document.getElementById('mtusize').value) - 2;
    const interleavedCount = parseInt(document.getElementById('interleavedcount').value);
    const count = Math.round(data.length / mtu);
    let chunkIdx = 0;
    let noReplyCount = interleavedCount;
    for (let i = 0; i < data.length; i += mtu) {
        const currentTime = (Date.now() - startTime) / 1000.0;
        setStatus(`${step === 'bw' ? '黑白' : '颜色'}块: ${chunkIdx + 1}/${count + 1}, 总用时: ${currentTime}s`);
        const payload = [
            (step === 'bw' ? 0x0F : 0x00) | (i === 0 ? 0x00 : 0xF0),
            ...data.slice(i, i + mtu)
        ];
        if (noReplyCount > 0) {
            await write(EpdCmd.WRITE_IMG, payload, false);
            noReplyCount--;
        } else {
            await write(EpdCmd.WRITE_IMG, payload, true);
            noReplyCount = interleavedCount;
        }
        chunkIdx++;
    }
}

// 使用CRC校验传输（如果固件支持）
async function writeImageCRC(data, step = 'bw') {
    const stepName = step === 'bw' ? '黑白' : '颜色';
    try {
        await BleTransfer.sendImageWithResume(data, step, (sent, total, speedInfo) => {
            if (speedInfo) {
                setStatus(`${stepName}块(CRC): ${sent}/${total}, ${BleTransfer.getSpeedString()}, ${speedInfo.elapsed}s`);
            } else {
                setStatus(`${stepName}块(CRC): ${sent}/${total}`);
            }
        });
        return true;
    } catch (e) {
        console.error('CRC transfer failed:', e);
        addLog(`CRC传输失败: ${e.message}，回退到普通传输`);
        await writeImage(data, step);
        return true;
    }
}

// ==================== 设备控制 ====================
async function setDriver() {
    if (!confirm('确认设置驱动配置？此操作将重新初始化屏幕。')) return;
    await write(EpdCmd.SET_PINS, document.getElementById("epdpins").value);
    await write(EpdCmd.INIT, document.getElementById("epddriver").value);
    addLog("驱动配置已设置");
}

function getWeekStart() {
    const weekStartValue = document.getElementById('weekStart').value;
    return weekStartValue !== null && weekStartValue !== '' ? parseInt(weekStartValue) : 1;
}

function buildTimeData(mode) {
    const timestamp = new Date().getTime() / 1000;
    return new Uint8Array([
        (timestamp >> 24) & 0xFF,
        (timestamp >> 16) & 0xFF,
        (timestamp >> 8) & 0xFF,
        timestamp & 0xFF,
        -(new Date().getTimezoneOffset() / 60),
        mode
    ]);
}

async function sendTimeCommand(mode, modeName) {
    const weekStart = getWeekStart();
    const weekDays = ['周日', '周一', '周二', '周三', '周四', '周五', '周六'];
    await write(EpdCmd.SET_WEEK_START, new Uint8Array([weekStart]));
    if (await write(EpdCmd.SET_TIME, buildTimeData(mode))) {
        addLog(`${modeName}已启用！`);
        addLog(`星期第一天已设置为：${weekDays[weekStart]}`);
        addLog("屏幕刷新完成前请不要操作。");
        return true;
    }
    return false;
}

// 老款时钟模式 (仅适用于UC8179 7.5寸)
async function syncTimeLegacy() {
    if (!confirm('确认切换到老款时钟模式？\n\n⚠️ 警告：时钟模式会加速屏幕老化导致损坏！\n• 请勿长时间使用\n• 此模式仅适用于UC8179 7.5寸屏幕\n• 费电')) return;
    await sendTimeCommand(3, '老款时钟模式');
}

async function syncTime(mode) {
    if (mode === 2 && !confirm("提醒：时钟模式目前使用全刷实现，此功能目前多用于修复老化屏残影问题，不建议长期开启，是否继续？")) return;
    if (mode === 1) {
        await syncHolidayData();
        await sleep(200);
    }
    const timestamp = Math.floor(Date.now() / 1000);
    const data = new Uint8Array([
        (timestamp >> 24) & 0xFF,
        (timestamp >> 16) & 0xFF,
        (timestamp >> 8) & 0xFF,
        timestamp & 0xFF,
        -(new Date().getTimezoneOffset() / 60),
        mode
    ]);
    if (await write(EpdCmd.SET_TIME, data)) {
        addLog("时间已同步！");
        addLog("屏幕刷新完成前请不要操作。");
    }
    await sendTimeCommand(mode, mode === 1 ? '日历模式' : '时钟模式');
}

// ==================== 新增：无弹窗同步时间 ====================
async function syncTimeOnly() {
    // 仅对 Web 模式有效，APP 模式下该按钮应被隐藏
    const timestamp = Math.floor(Date.now() / 1000);
    const data = new Uint8Array([
        (timestamp >> 24) & 0xFF,
        (timestamp >> 16) & 0xFF,
        (timestamp >> 8) & 0xFF,
        timestamp & 0xFF,
        -(new Date().getTimezoneOffset() / 60),
        1   // 日历模式（仅为时间同步，模式值随意）
    ]);
    const success = await write(EpdCmd.SET_TIME, data);
    if (success) {
        addLog(`✅ 已同步时间: ${new Date(timestamp * 1000).toLocaleString()}`);
    } else {
        addLog("❌ 时间同步失败");
    }
}

async function clearScreen() {
    if (confirm('确认清除屏幕内容?')) {
        await write(EpdCmd.CLEAR);
        addLog("清屏指令已发送！");
        addLog("屏幕刷新完成前请不要操作。");
    }
}

async function sendcmd() {
    const cmdTXT = document.getElementById('cmdTXT').value;
    if (!cmdTXT) return;
    const bytes = hex2bytes(cmdTXT);
    await write(bytes[0], bytes.length > 1 ? bytes.slice(1) : null);
    addLog("命令已发送");
}

// ==================== 残影消除模式 ====================
async function startGhostingClearModeAsync(cycles) {
    const data = new Uint8Array([(cycles >> 8) & 0xFF, cycles & 0xFF]);
    await write(EpdCmd.GHOSTING_CLEAR, data, false);
    addLog(`残影消除模式已启动，将执行 ${cycles} 次清除流程`);
    addLog("指令已发送，准备自动断开连接...");
    setTimeout(() => {
        if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
            bleDevice.gatt.disconnect();
        }
    }, 300);
}

function startGhostingClearMode() {
    const cyclesInput = document.getElementById('ghostingCycles');
    let cycles = parseInt(cyclesInput.value);
    if (isNaN(cycles) || cycles < 1 || cycles > 1000) {
        alert("请输入有效的执行次数（1-1000）");
        return;
    }
    if (confirm(`确认要执行残影消除模式吗？\n将执行 ${cycles} 次清除流程\n每次流程约需30秒\n总计约需 ${Math.ceil(30 * cycles / 60)} 分钟`)) {
        setTimeout(() => {
            startGhostingClearModeAsync(cycles).catch(e => {
                console.error(e);
                addLog("残影消除指令发送失败");
            });
        }, 0);
    }
}

function stopGhostingClearMode() {
    if (confirm("确认要退出残影消除模式吗？")) {
        setTimeout(() => {
            write(EpdCmd.GHOSTING_STOP, null, false).then(success => {
                if (success) addLog("已发送退出残影消除指令");
            }).catch(e => {
                console.error(e);
                addLog("退出残影消除指令发送失败");
            });
        }, 0);
    }
}

// ==================== UC8159 特殊转换 ====================
function convertUC8159(blackWhiteData, redWhiteData) {
    const halfLength = blackWhiteData.length;
    const payloadData = new Uint8Array(halfLength * 4);
    let idx = 0;
    for (let i = 0; i < halfLength; i++) {
        let black = blackWhiteData[i];
        let red = redWhiteData[i];
        for (let j = 0; j < 8; j++) {
            let data;
            if ((red & 0x80) === 0) data = 0x04;
            else if ((black & 0x80) === 0) data = 0x00;
            else data = 0x03;
            data = (data << 4) & 0xFF;
            black = (black << 1) & 0xFF;
            red = (red << 1) & 0xFF;
            j++;
            if ((red & 0x80) === 0) data |= 0x04;
            else if ((black & 0x80) === 0) data |= 0x00;
            else data |= 0x03;
            black = (black << 1) & 0xFF;
            red = (red << 1) & 0xFF;
            payloadData[idx++] = data;
        }
    }
    return payloadData;
}

// ==================== APP模式专用函数 ====================
async function loadImageFromFile(file) {
    return new Promise((resolve, reject) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = reject;
        img.src = URL.createObjectURL(file);
    });
}

async function sendimgAppMode() {
    const epdIndex = parseInt(document.getElementById('abSelect')?.value || '1');
    const ditherEnabled = document.getElementById('ditherEnable')?.checked || false;
    const compressEnabled = document.getElementById('compressEnable')?.checked || false;
    const epdTypeVal = 0x06;  // 七色屏固定

    // 获取 A 面原始数据（优先使用同步数据，否则从文件读取）
    let sourceImageDataA = storedImageDataA;
    let sourceImageDataB = storedImageDataB;

    const fileA = document.getElementById('imageFileA')?.files[0];
    const fileB = document.getElementById('imageFileB')?.files[0];

    // 辅助函数：从文件加载 ImageData
    async function loadImageDataFromFile(file, width, height) {
        const img = await loadImageFromFile(file);
        const offCanvas = document.createElement('canvas');
        offCanvas.width = width;
        offCanvas.height = height;
        const offCtx = offCanvas.getContext('2d');
        offCtx.drawImage(img, 0, 0, width, height);
        return offCtx.getImageData(0, 0, width, height);
    }

    // 若没有同步数据但有文件，则从文件加载
    if (!sourceImageDataA && fileA) {
        sourceImageDataA = await loadImageDataFromFile(fileA, canvas.width, canvas.height);
        addLog("📁 从 A 面文件加载图片");
    }
    if (!sourceImageDataB && fileB && (epdIndex === 7)) {
        sourceImageDataB = await loadImageDataFromFile(fileB, canvas.width, canvas.height);
        addLog("📁 从 B 面文件加载图片");
    }

    // 根据模式检查数据完整性
    if (epdIndex === 3) {
        // A&B 同显：只需 A 面
        if (!sourceImageDataA) {
            alert("请先通过「从主画布同步到 A 面」上传 A 面内容，或选择 A 面图片文件！");
            return;
        }
    } else if (epdIndex === 7) {
        // A&B 异显：需要 A 面和 B 面
        if (!sourceImageDataA || !sourceImageDataB) {
            alert("AB 异显模式需要同时提供 A 面和 B 面内容！\n请分别同步或上传两张图片。");
            return;
        }
    } else {
        // 单面模式（A面显示，B面忽略）
        if (!sourceImageDataA) {
            alert("请先同步或上传 A 面内容！");
            return;
        }
    }

    // 对每个面的图像数据进行抖动预处理（如果启用）
    let processedImageDataA = sourceImageDataA;
    let processedImageDataB = sourceImageDataB;
    if (ditherEnabled) {
        addLog("🎨 正在对图像进行抖动预处理...");
        const contrast = parseFloat(document.getElementById('ditherContrast').value);
        const brightness = parseFloat(document.getElementById('ditherBrightness').value);
        const alg = document.getElementById('ditherAlg').value;
        const strength = parseFloat(document.getElementById('ditherStrength').value);
        const colorMode = document.getElementById('ditherMode').value;
        
        // 对 A 面处理
        let tempA = new ImageData(new Uint8ClampedArray(sourceImageDataA.data), canvas.width, canvas.height);
        if (!isNaN(contrast)) tempA = adjustContrast(tempA, contrast);
        if (!isNaN(brightness) && brightness !== 1.0) tempA = adjustBrightness(tempA, brightness);
        if (alg !== 'none') tempA = ditherImage(tempA, alg, strength, colorMode);
        processedImageDataA = tempA;
        
        // 对 B 面（若有）
        if (sourceImageDataB) {
            let tempB = new ImageData(new Uint8ClampedArray(sourceImageDataB.data), canvas.width, canvas.height);
            if (!isNaN(contrast)) tempB = adjustContrast(tempB, contrast);
            if (!isNaN(brightness) && brightness !== 1.0) tempB = adjustBrightness(tempB, brightness);
            if (alg !== 'none') tempB = ditherImage(tempB, alg, strength, colorMode);
            processedImageDataB = tempB;
        }
        addLog("✅ 抖动预处理完成");
    }

    // 转换为设备数据格式
    let dataA = null, dataB = null;
    try {
        dataA = EpdFormat.convertWithType(epdTypeVal, canvas.width, canvas.height, processedImageDataA, findClosestColor);
    } catch (e) {
        addLog("❌ A 面格式转换失败: " + e.message);
        return;
    }
    if (processedImageDataB) {
        try {
            dataB = EpdFormat.convertWithType(epdTypeVal, canvas.width, canvas.height, processedImageDataB, findClosestColor);
        } catch (e) {
            addLog("❌ B 面格式转换失败: " + e.message);
            return;
        }
    }

    let finalData = null;
    if (epdIndex === 3) {
        // 同显：只用 A 面数据
        finalData = dataA;
        addLog(`同显模式：发送 A 面数据，长度 ${dataA.length} 字节`);
    } else if (epdIndex === 7) {
        // 异显：拼接 A 面 + B 面
        finalData = new Uint8Array(dataA.length + dataB.length);
        finalData.set(dataA, 0);
        finalData.set(dataB, dataA.length);
        addLog(`异显模式：发送 A+B 面数据，总长度 ${finalData.length} 字节`);
    } else {
        // 其他值（1 或 2）默认为单面显示 A 面
        finalData = dataA;
        addLog(`单面模式：发送 A 面数据，长度 ${dataA.length} 字节`);
    }

    if (!finalData) {
        addLog("❌ 未生成有效的设备数据，请检查图像内容。");
        return;
    }

    // 发送
    startTime = Date.now();
    const statusEl = document.getElementById("status");
    statusEl.parentElement.style.display = "block";

    try {
        AppProtocol.setEpdType(epdTypeVal);
        AppProtocol.setEpdIndex(epdIndex);
        AppProtocol.setCompress(compressEnabled);
        AppProtocol.setProgressCallback((sent, total) => {
            const elapsed = (Date.now() - startTime) / 1000;
            setStatus(`发送图片: ${sent}/${total} 包, 用时 ${elapsed.toFixed(1)}s`);
        });
        AppProtocol.setCompleteCallback(() => {
            if (bleDevice && bleDevice.gatt && bleDevice.gatt.connected) {
                addLog("✅ APP模式传输完成（刷新等待时间长, 请耐心等待设备刷新）");
            }
        });

        addLog(`准备发送图片，模式: 七色, 压缩: ${compressEnabled}, 驱动: 0x${epdTypeVal.toString(16)}`);
        await AppProtocol.sendFullImage(finalData, 'sevenColor', epdTypeVal, compressEnabled);
        const elapsed = (Date.now() - startTime) / 1000;
        addLog(`✅ APP 模式发送完成！耗时: ${elapsed.toFixed(2)}s`);
    } catch (e) {
        addLog(`❌ APP 模式发送失败: ${e.message}`);
        console.error(e);
    } finally {
        updateButtonStatus();
        setTimeout(() => { statusEl.parentElement.style.display = "none"; }, 5000);
    }
}

// ==================== 发送图片（支持双协议）====================
async function sendimg() {
    if (cropManager.isCropMode()) {
        alert("请先完成图片裁剪！发送已取消。");
        return;
    }

    const hasSpecialContent = paintManager && (
        (paintManager.scheduleData && paintManager.scheduleData.length > 0) ||
        (paintManager.todoData && paintManager.todoData.length > 0) ||
        paintManager.cardData ||
        paintManager.wifiData
    );

    if (appModeEnabled) {
        await sendimgAppMode();
        return;
    }

    // ========== 原始网页版发送逻辑 ==========
    if (hasSpecialContent) {
        addLog("特殊内容发送：重绘画布（禁用抖动/对比度，直接按渲染结果发送）");
        paintManager.redrawAll();
    } else {
        if (typeof convertDithering === 'function') convertDithering();
    }

    const canvasSizeVal = document.getElementById('canvasSize').value;
    const ditherMode = document.getElementById('ditherMode').value;
    const epdDriverSelect = document.getElementById('epddriver');
    const selectedOption = epdDriverSelect.options[epdDriverSelect.selectedIndex];

    if (selectedOption.getAttribute('data-size') !== canvasSizeVal && !confirm("警告：画布尺寸和驱动不匹配，是否继续？")) return;
    if (selectedOption.getAttribute('data-color') !== ditherMode && !confirm("警告：颜色模式和驱动不匹配，是否继续？")) return;

    startTime = Date.now();
    const statusEl = document.getElementById("status");
    statusEl.parentElement.style.display = "block";

    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const processedData = processImageData(imageData, ditherMode);

    updateButtonStatus(true);
    await write(EpdCmd.INIT);

    const useCRC = (appVersion >= 0x20) && typeof BleTransfer !== 'undefined';
    const transferFn = useCRC ? writeImageCRC : writeImage;
    if (useCRC) addLog("使用CRC校验传输模式");

    if (ditherMode === 'fourColor') {
        await transferFn(processedData, 'color');
    } else if (ditherMode === 'threeColor') {
        const half = Math.floor(processedData.length / 2);
        const bwData = processedData.slice(0, half);
        const redData = processedData.slice(half);
        if (epdDriverSelect.value === '08' || epdDriverSelect.value === '09') {
            await transferFn(convertUC8159(bwData, redData), 'bw');
        } else {
            await transferFn(bwData, 'bw');
            await transferFn(redData, 'red');
        }
    } else if (ditherMode === 'blackWhiteColor') {
        if (epdDriverSelect.value === '08' || epdDriverSelect.value === '09') {
            const empty = new Uint8Array(processedData.length).fill(0xFF);
            await transferFn(convertUC8159(processedData, empty), 'bw');
        } else {
            await transferFn(processedData, 'bw');
        }
    } else {
        addLog("当前固件不支持此颜色模式。");
        updateButtonStatus();
        return;
    }

    await write(EpdCmd.REFRESH);
    updateButtonStatus();

    const elapsed = (Date.now() - startTime) / 1000;
    addLog(`发送完成！耗时: ${elapsed}s`);
    setStatus(`发送完成！耗时: ${elapsed}s`);
    addLog("屏幕刷新完成前请不要操作。");
    setTimeout(() => {
        statusEl.parentElement.style.display = "none";
    }, 5000);
}

// ==================== 下载/上传数组 ====================
function downloadDataArray() {
    if (cropManager.isCropMode()) {
        alert("请先完成图片裁剪！下载已取消。");
        return;
    }

    const hasSpecial = paintManager && (
        (paintManager.scheduleData && paintManager.scheduleData.length > 0) ||
        (paintManager.todoData && paintManager.todoData.length > 0) ||
        paintManager.cardData ||
        paintManager.wifiData
    );
    if (hasSpecial) {
        addLog("特殊内容下载：重绘画布（禁用抖动/对比度，直接导出PCF渲染结果）");
        paintManager.redrawAll();
    }

    const mode = document.getElementById('ditherMode').value;
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    const processedData = processImageData(imageData, mode);

    const hexLines = [];
    for (let i = 0; i < processedData.length; i++) {
        hexLines.push("0x" + (processedData[i] & 0xFF).toString(16).padStart(2, '0'));
    }
    const chunks = [];
    for (let i = 0; i < hexLines.length; i += 16) {
        chunks.push(hexLines.slice(i, i + 16).join(', '));
    }

    const colorModeCode = mode === 'sixColor' ? 0 : mode === 'fourColor' ? 1 : mode === 'blackWhiteColor' ? 2 : 3;
    const content = [
        'const uint8_t imageData[] PROGMEM = {',
        chunks.join(',\n'),
        '};',
        `const uint16_t imageWidth = ${canvas.width};`,
        `const uint16_t imageHeight = ${canvas.height};`,
        `const uint8_t colorMode = ${colorModeCode};`
    ].join('\n');

    const blob = new Blob([content], { type: 'text/plain' });
    const link = document.createElement('a');
    link.download = 'imagedata.h';
    link.href = URL.createObjectURL(blob);
    link.click();
    URL.revokeObjectURL(link.href);
}

function downloadImage() {
    // 将当前 canvas 内容保存为 PNG 文件，尺寸保持原样
    const link = document.createElement('a');
    link.download = 'epd_image.png';
    link.href = canvas.toDataURL('image/png');
    link.click();
}

function uploadDataArray() {
    if (cropManager.isCropMode()) {
        alert("请先完成图片裁剪！上传已取消。");
        return;
    }

    const fileInput = document.createElement('input');
    fileInput.type = 'file';
    fileInput.accept = '.h';
    fileInput.style.display = 'none';
    document.body.appendChild(fileInput);

    fileInput.addEventListener('change', function(e) {
        const file = e.target.files[0];
        if (!file) {
            document.body.removeChild(fileInput);
            return;
        }

        const reader = new FileReader();
        reader.onload = function(ev) {
            try {
                const content = ev.target.result;
                const widthMatch = content.match(/const uint16_t imageWidth\s*=\s*(\d+);/);
                const heightMatch = content.match(/const uint16_t imageHeight\s*=\s*(\d+);/);
                const colorModeMatch = content.match(/const uint8_t colorMode\s*=\s*(\d+);/);
                const dataMatch = content.match(/const uint8_t imageData\[\]\s+PROGMEM\s*=\s*\{([^}]+)\}/s);

                if (!widthMatch || !heightMatch || !colorModeMatch || !dataMatch) {
                    alert("无法解析数组文件，请确保文件格式与下载的一致。");
                    return;
                }

                const width = parseInt(widthMatch[1], 10);
                const height = parseInt(heightMatch[1], 10);
                const code = parseInt(colorModeMatch[1], 10);
                let modeStr;
                if (code === 0) modeStr = "sixColor";
                else if (code === 1) modeStr = "fourColor";
                else if (code === 2) modeStr = "blackWhiteColor";
                else if (code === 3) modeStr = "threeColor";
                else throw new Error("未知颜色模式码");

                const dataStr = dataMatch[1].replace(/\s+/g, '');
                const numbers = dataStr.split(',').filter(s => s.length).map(s => {
                    s = s.trim();
                    if (s.startsWith('0x') || s.startsWith('0X')) return parseInt(s.substring(2), 16);
                    return parseInt(s, 10);
                });
                const dataArray = new Uint8Array(numbers);

                let expected;
                if (modeStr === "sixColor") expected = width * height;
                else if (modeStr === "fourColor") expected = Math.ceil(width * height / 4);
                else if (modeStr === "threeColor") expected = Math.ceil(width * height / 8) * 2;
                else expected = Math.ceil(width * height / 8);
                if (dataArray.length !== expected) {
                    alert(`数组长度不匹配：预期 ${expected} 字节，实际 ${dataArray.length} 字节。`);
                    return;
                }

                canvas.width = width;
                canvas.height = height;
                const sizeSelect = document.getElementById('canvasSize');
                if (sizeSelect) {
                    const matchOpt = Array.from(sizeSelect.options).find(opt => {
                        const [_, w, h] = opt.value.split('_');
                        return parseInt(w, 10) === width && parseInt(h, 10) === height;
                    });
                    if (matchOpt) sizeSelect.value = matchOpt.value;
                }
                document.getElementById('ditherMode').value = modeStr;

                const decoded = decodeProcessedData(dataArray, width, height, modeStr);
                ctx.putImageData(decoded, 0, 0);
                if (paintManager) {
                    paintManager.clearElements();
                    paintManager.saveToHistory();
                }
                addLog(`✅ 已从文件加载数组：${width}x${height}，模式：${modeStr}，共 ${dataArray.length} 字节`);
            } catch (err) {
                console.error(err);
                alert("解析文件时出错：" + err.message);
            } finally {
                document.body.removeChild(fileInput);
            }
        };
        reader.readAsText(file);
    });
    fileInput.click();
}

// ==================== UI 辅助 ====================
function updateButtonStatus(forceDisabled = false) {
    const connected = gattServer && gattServer.connected;
    const disabled = forceDisabled || !connected ? 'disabled' : null;
    document.getElementById("reconnectbutton").disabled = (gattServer && gattServer.connected) ? 'disabled' : null;
    document.getElementById("sendcmdbutton").disabled = disabled;
    document.getElementById("calendarmodebutton").disabled = disabled;
    document.getElementById("clockmodebutton").disabled = disabled;
    document.getElementById("clearscreenbutton").disabled = disabled;
    document.getElementById("sendimgbutton").disabled = disabled;
    document.getElementById("setDriverbutton").disabled = disabled;
    document.getElementById("syncholidaybutton").disabled = disabled;
    const testBtn = document.querySelector('button[onclick="syncAndShowCalendar()"]');
    if(testBtn) testBtn.disabled = disabled;
}

function disconnect() {
    updateButtonStatus();
    resetVariables();
    addLog('已断开连接.');
    document.getElementById("connectbutton").innerHTML = '连接';
}

// ==================== 根据协议显示/隐藏功能区 ====================
function updateUIBasedOnProtocol() {
    const webOnlyIds = ['calendarmodebutton','clockmodebutton','clearscreenbutton','syncholidaybutton','importholidaybutton','holidayhelpbutton','testTimestamp','testYear','testMonth'];
    const appOnlyIds = ['abSelectGroup','compressOptionGroup','ditherEnableGroup','doubleImagePanel'];
    const protocolSpan = document.getElementById('protocolStatus');
    if (protocolSpan) {
        if (appModeEnabled) {
            protocolSpan.textContent = 'APP 模式';
            protocolSpan.style.color = '#4CAF50';
        } else {
            protocolSpan.textContent = '网页模式 (Web)';
            protocolSpan.style.color = '#2196F3';
        }
    }

    if (appModeEnabled) {
        // 隐藏 Web 特有功能
        webOnlyIds.forEach(id => {
            const el = document.getElementById(id);
            if (el) {
                if (el.tagName === 'BUTTON' || el.tagName === 'INPUT' || el.tagName === 'SELECT') {
                    el.disabled = true;
                } else {
                    el.style.display = 'none';
                }
            }
        });
        // 显示 APP 特有功能
        appOnlyIds.forEach(id => {
            const el = document.getElementById(id);
            if (el) el.style.display = '';
        });
        //显示图像颜色和抖动操作栏
        const imagePanel = document.getElementById('image-panel');
        const imageModeBtn = document.getElementById('image-mode');
        if (imagePanel && imagePanel.style.display === 'none') {
            imagePanel.style.display = '';
            if (imageModeBtn) imageModeBtn.classList.add('active');
        }

        // 查找包含 #testYear 的 .flex-container.debug 并隐藏（不使用 :has 选择器）
        const testYearElement = document.getElementById('testYear');
        if (testYearElement) {
            let container = testYearElement.closest('.flex-container.debug');
            if (container) container.style.display = 'none';
        }
        // 隐藏残影消除相关区域
        const ghostingCyclesElement = document.getElementById('ghostingCycles');
        if (ghostingCyclesElement) {
            let container = ghostingCyclesElement.closest('.flex-container.debug');
            if (container) container.style.display = 'none';
        }
    } else {
        // Web 模式：恢复所有功能
        webOnlyIds.forEach(id => {
            const el = document.getElementById(id);
            if (el) {
                if (el.tagName === 'BUTTON' || el.tagName === 'INPUT' || el.tagName === 'SELECT') {
                    el.disabled = false;
                } else {
                    el.style.display = '';
                }
            }
        });
        appOnlyIds.forEach(id => {
            const el = document.getElementById(id);
            if (el) el.style.display = 'none';
        });

        // 恢复假期测试区域
        const testYearElement = document.getElementById('testYear');
        if (testYearElement) {
            let container = testYearElement.closest('.flex-container.debug');
            if (container) container.style.display = '';
        }
        // 恢复残影消除区域
        const ghostingCyclesElement = document.getElementById('ghostingCycles');
        if (ghostingCyclesElement) {
            let container = ghostingCyclesElement.closest('.flex-container.debug');
            if (container) container.style.display = '';
        }
    }
    //增加对 APP 模式状态栏的显示
    const statusBar = document.getElementById('appModeStatusBar');
    if (statusBar) {
        if (appModeEnabled) {
            statusBar.style.display = 'block';
            statusBar.innerHTML = `📌 APP 模式：当前画布尺寸 ${canvas.width}x${canvas.height} | A面${storedImageDataA ? '已设置' : '未设置'} | B面${storedImageDataB ? '已设置' : '未设置'}`;
        } else {
            statusBar.style.display = 'none';
        }
    }
}

// ==================== 蓝牙连接相关（自动检测协议）====================
async function filterConnect() {
    await preConnect(true, true);
}

async function preConnect(useFilter = false, forceNew = false) {
    if (gattServer && gattServer.connected) {
    	if (bleDevice && bleDevice.gatt.connected) bleDevice.gatt.disconnect();
    	if (!forceNew) return; await sleep(300);
    }
    resetVariables();
    try {
        const filterInput = document.getElementById('blenamefilter');
        const filterValue = filterInput?.value.trim();
        if (filterInput) filterInput.blur();
        const options = {
        	optionalServices: [
        		'62750001-d828-918d-fb46-b6c11c675aec', 
        		'0000ff01-0000-1000-8000-00805f9b34fb'
        ] };
        if (useFilter && filterValue && filterValue.length > 0) {
            const prefix = filterValue.toUpperCase();
            options.filters = [{ namePrefix: 'NRF_EPD_' + prefix }, { namePrefix: 'EPD_' + prefix }];
            addLog(`按名称过滤: NRF_EPD_${prefix} 或 EPD_${prefix}`);
        } else {
        	options.acceptAllDevices = true;
        }
        bleDevice = await navigator.bluetooth.requestDevice(options);
        addLog(`已选择设备: ${bleDevice.name || bleDevice.id}`);
    } catch (e) {
        if (e.name === 'NotFoundError' || (e.message && e.message.includes('User cancelled'))) addLog("已取消设备选择。");
        else {
        	console.error(e); 
        	if (e.message) addLog("requestDevice: " + e.message); 
        	addLog("请检查蓝牙是否已开启，且使用的浏览器支持蓝牙！建议使用以下浏览器：");
            addLog("• 电脑: Chrome/Edge");
            addLog("• Android: Chrome/Edge");
            addLog("• iOS: Bluefy 浏览器");
        }
        return;
    }
    bleDevice.addEventListener('gattserverdisconnected', disconnect);
    setTimeout(async () => { await connect(); }, 300);
}

async function reConnect() {
    if (bleDevice && bleDevice.gatt.connected) bleDevice.gatt.disconnect();
    resetVariables();
    addLog("正在重连");
    setTimeout(async () => { await connect(); }, 300);
}

async function connect() {
    if (!bleDevice || epdCharacteristic) return;
    try {
        addLog("正在连接: " + bleDevice.name);
        gattServer = await bleDevice.gatt.connect();
        addLog("  找到 GATT Server");
        // 先尝试 Web 协议
        try {
            epdService = await gattServer.getPrimaryService('62750001-d828-918d-fb46-b6c11c675aec');
            addLog("  找到 EPD Service (Web 协议)");
            epdCharacteristic = await epdService.getCharacteristic('62750002-d828-918d-fb46-b6c11c675aec');
            addLog("  找到 RX Characteristic");
            txCharacteristic = await epdService.getCharacteristic('62750003-d828-918d-fb46-b6c11c675aec');
            addLog("  找到 TX Characteristic");
            await epdCharacteristic.startNotifications();
            epdCharacteristic.addEventListener('characteristicvaluechanged', (event) => { handleNotify(event.target.value, msgIndex++); });
            addLog("  通知已开启");
            await sleep(50);
            appModeEnabled = false;
            addLog("📡 协议模式: 网页模式");
        } catch (e) {
            addLog("Web 协议识别失败，尝试 APP 协议...");
            try {
                epdService = await gattServer.getPrimaryService('0000ff01-0000-1000-8000-00805f9b34fb');
                addLog("  找到 EPD Service (APP 协议)");
                cmdCharacteristic = await epdService.getCharacteristic('0000ff03-0000-1000-8000-00805f9b34fb');
                addLog("  找到 WriteCMD Characteristic (0000ff03)");
                epdCharacteristic = await epdService.getCharacteristic('0000ff02-0000-1000-8000-00805f9b34fb');
                addLog("  找到 WritePic Characteristic (0000ff02)");
                txCharacteristic = await epdService.getCharacteristic('0000ff04-0000-1000-8000-00805f9b34fb');
                addLog("  找到 Notify Characteristic (0000ff04)");

                try { 
                    await txCharacteristic.startNotifications(); 
                    txCharacteristic.addEventListener('characteristicvaluechanged', (event) => { 
                        handleNotify(event.target.value, msgIndex++); 
                    }); 
                    addLog("  通知已开启");
                } catch(e2){ 
                    addLog("  通知开启失败（不影响写操作）: "+e2.message);
                }
                appModeEnabled = true;
                addLog("📡 协议模式: APP 模式");

                if (typeof AppProtocol !== 'undefined') {
                    AppProtocol.setCharacteristics(cmdCharacteristic, epdCharacteristic);
                    AppProtocol.setNotifyCharacteristic(txCharacteristic);   // 新增这一行
                    AppProtocol.setLogCallback(addLog);   // 添加这一行
                    
                    // ========== MTU 协商（大幅提升速度） ==========
                    let actualMtu = 23;
                    try {
                        // 请求 MTU = 256
                        await gattServer.requestMTU(256);
                        addLog("  已请求 MTU=256");
                        await sleep(500);
                        // 获取协商后的实际 MTU（不同浏览器位置不同）
                         if (gattServer.mtu) {
                             actualMtu = gattServer.mtu;
                             addLog(`  协商实际 MTU (gattServer.mtu) = ${actualMtu}`);
                          } else if (cmdCharacteristic.service.device.gatt && cmdCharacteristic.service.device.gatt.mtu) {
                              actualMtu = cmdCharacteristic.service.device.gatt.mtu;
                              addLog(`  协商实际 MTU (device.gatt.mtu) = ${actualMtu}`);
                          } else {
                              addLog(`  ⚠️ 无法获取实际 MTU，使用默认 23`);
                          }
                    } catch(mtuErr) {
                         addLog(`  MTU 协商失败: ${mtuErr.message}，使用默认 23`);
                    }
                    // 关键：将 MTU 应用到 AppProtocol（负载大小 = MTU - 3）
                    AppProtocol.setMtuSize(actualMtu);
                    addLog(`  ✅ APP模式数据包负载大小 = ${actualMtu - 3} 字节`);
                    
                    // 强制设置七色屏驱动（保持不变）
                    const epdDriverSelect = document.getElementById('epddriver');
                    // 强制添加并选择 0x06 七色驱动
                    let option = Array.from(epdDriverSelect.options).find(opt => opt.value === 'FF');
                    if (!option) {
                        option = document.createElement('option');
                        option.value = 'FF';
                        option.setAttribute('data-color', 'sevenColor');
                        option.setAttribute('data-size', '7.3E6_800_480');
                        option.text = '7.3寸 (七色, Spectra 6)';
                        epdDriverSelect.appendChild(option);
                    }
                    epdDriverSelect.value = 'FF';
                    const ditherModeSelect = document.getElementById('ditherMode');
                    if (ditherModeSelect) ditherModeSelect.value = 'sevenColor';
                    const canvasSizeSelect = document.getElementById('canvasSize');
                    if (canvasSizeSelect) canvasSizeSelect.value = '7.3E6_800_480';
                    updateCanvasSize();
                    addLog("✅ APP 模式：已强制设置为七色 7.3 寸屏幕 (Spectra 6)");
                    AppProtocol.setEpdType(0x06);
                    const abSelect = document.getElementById('abSelect');
                    AppProtocol.setEpdIndex(abSelect ? parseInt(abSelect.value) : 1);
                    const compressCheck = document.getElementById('compressEnable');
                    AppProtocol.setCompress(compressCheck ? compressCheck.checked : false);
                    addLog("  AppProtocol 初始化完成");
                } else { addLog("  警告：AppProtocol 未加载，APP 模式无法发送图片"); }
            } catch (e2) { throw new Error("无法识别设备协议，请确认设备固件是否支持"); }
        }
        updateUIBasedOnProtocol();
        if (!appModeEnabled) {
            try { const versionData = await txCharacteristic.readValue(); appVersion = versionData.getUint8(0); addLog(`固件版本: 0x${appVersion.toString(16)}`); addLog(`APP版本: v${APP_VERSION} (${APP_BUILD_DATE})`); } catch(e){ appVersion=0x15; }
            if (typeof BleTransfer !== 'undefined') BleTransfer.init();
            await write(EpdCmd.INIT);
        } else {
            appVersion = 0x20;
            addLog("APP 模式：固件版本假定为 0x20");
        }
        if (!appModeEnabled && appVersion < 0x16) {
            const oldURL = "https://tsl0922.github.io/EPD-nRF5/v1.5";
            alert("!!!注意!!!\n当前固件版本过低，可能无法正常使用部分功能，建议升级到最新版本。");
            if (confirm('是否访问旧版本上位机？')) location.href = oldURL;
            setTimeout(()=> addLog(`如遇到问题，可访问旧版本上位机: ${oldURL}`), 500);
        }
        document.getElementById("connectbutton").innerHTML = '断开';
        updateButtonStatus();
        addLog("✅ 连接成功，可以发送指令或图片");
    } catch (e) {
        console.error(e);
        if (e.message) addLog("connect: " + e.message);
        disconnect();
        return;
    }
}

function handleNotify(value, idx) {
    const data = new Uint8Array(value.buffer, value.byteOffset, value.byteLength);

    // APP 模式：Notify 用于 CRC 传输的 ACK/状态，或者忽略
    if (appModeEnabled) {
        // 如果有 CRC 传输模块，让其处理（BleTransfer 未在 APP 模式使用，但保留以防万一）
        return;
    }

    // ========== 以下仅 Web 协议 ==========
    // CRC 传输处理
    if (data.length >= 1 && (data[0] === 0xA0 || data[0] === 0xA1)) {
        if (typeof BleTransfer !== 'undefined') BleTransfer.handleNotification(value);
        return;
    }

    // idx === 0: 设备主动上报配置（仅 Web 协议）
    if (idx === 0) {
        addLog(`收到配置：${bytes2hex(data)}`);
        const epdpins = document.getElementById("epdpins");
        const epddriver = document.getElementById("epddriver");
        if (epdpins) {
            epdpins.value = bytes2hex(data.slice(0, 7));
            if (data.length > 10) epdpins.value += bytes2hex(data.slice(10, 11));
        }
        // 驱动 ID 为小写 hex（如 "23"），选项值大小写不统一，故忽略大小写匹配
        const driverId = bytes2hex(data.slice(7, 8));
        if (epddriver) {
            const match = Array.from(epddriver.options)
                .find(o => o.value.toLowerCase() === driverId.toLowerCase());
            if (match) {
                epddriver.value = match.value;
            } else {
                addLog(`⚠️ 驱动 ID 0x${driverId} 不在当前驱动预设中，请切换到含该驱动的预设`);
            }
        }
        updateDitcherOptions();
    } else {
        // 普通文本消息
        if (!textDecoder) textDecoder = new TextDecoder();
        const msg = textDecoder.decode(data);
        addLog(msg, '⇓');
        if (msg.startsWith('mtu=') && msg.length > 4) {
            const mtu = parseInt(msg.substring(4));
            document.getElementById('mtusize').value = mtu;
            addLog(`MTU 已更新为: ${mtu}`);
        } else if (msg.startsWith('t=') && msg.length > 2) {
            const t = parseInt(msg.substring(2)) + (new Date().getTimezoneOffset() * 60);
            addLog(`远端时间: ${new Date(t * 1000).toLocaleString()}`);
            addLog(`本地时间: ${new Date().toLocaleString()}`);
        }
    }
}

// ==================== 日志和状态 ====================
function setStatus(text) {
    const el = document.getElementById("status");
    if (el) el.innerHTML = text;
}

function addLog(msg, action = '') {
    const logDiv = document.getElementById("log");
    if (!logDiv) return;
    const now = new Date();
    const timeStr = `${String(now.getHours()).padStart(2,'0')}:${String(now.getMinutes()).padStart(2,'0')}:${String(now.getSeconds()).padStart(2,'0')} `;
    const line = document.createElement('div');
    line.className = 'log-line';
    const timeSpan = document.createElement('span');
    timeSpan.className = 'time';
    timeSpan.textContent = timeStr;
    line.appendChild(timeSpan);
    if (action) {
        const actionSpan = document.createElement('span');
        actionSpan.className = 'action';
        actionSpan.innerHTML = action;
        line.appendChild(actionSpan);
    }
    line.appendChild(document.createTextNode(msg));
    logDiv.appendChild(line);
    logDiv.scrollTop = logDiv.scrollHeight;
    while (logDiv.childNodes.length > 200) logDiv.removeChild(logDiv.firstChild);
}

function clearLog() {
    const logDiv = document.getElementById("log");
    if (logDiv) logDiv.innerHTML = '';
}

// ==================== 画布操作 ====================
function fillCanvas(style) {
    ctx.fillStyle = style;
    ctx.fillRect(0, 0, canvas.width, canvas.height);
}

function setCanvasTitle(title) {
    const titleEl = document.querySelector('.canvas-title');
    if (titleEl) {
        titleEl.innerText = title;
        titleEl.style.display = title && title !== '' ? 'block' : 'none';
    }
}

function updateImage() {
    const fileInput = document.getElementById('imageFile');
    if (!fileInput.files.length) {
        fillCanvas('white');
        return;
    }
    const img = new Image();
    img.onload = () => {
        URL.revokeObjectURL(img.src);
        if (img.width / img.height === canvas.width / canvas.height) {
            if (cropManager.isCropMode()) cropManager.exitCropMode();
            ctx.drawImage(img, 0, 0, img.width, img.height, 0, 0, canvas.width, canvas.height);
            scheduleConvertDithering();
        } else {
            alert(`图片宽高比例与画布不匹配，将进入裁剪模式。\n请放大图片后移动图片使其充满画布, 再点击"完成"按钮。`);
            if (paintManager) paintManager.setActiveTool(null, '');
            cropManager.initializeCrop();
        }
    };
    img.src = URL.createObjectURL(fileInput.files[0]);
}

function updateCanvasSize() {
    const selected = document.getElementById('canvasSize').value;
    const size = canvasSizes.find(s => s.name === selected);
    canvas.width = size.width;
    canvas.height = size.height;
    updateImage();
}

function updateDitcherOptions() {
    const select = document.getElementById('epddriver');
    if (!select || select.selectedIndex < 0) return; // 未填充 / 设备上报的驱动 ID 不在当前预设中
    const opt = select.options[select.selectedIndex];
    if (!opt) return;
    const color = opt.getAttribute('data-color');
    const size = opt.getAttribute('data-size');
    if (color) {
        const mode = document.getElementById('ditherMode');
        if (mode) mode.value = color;
    }
    if (size) {
        const canvasSel = document.getElementById('canvasSize');
        if (canvasSel && Array.from(canvasSel.options).some(o => o.value === size)) canvasSel.value = size;
    }
    updateCanvasSize();
}

function rotateCanvas() {
    const w = canvas.width, h = canvas.height;
    const imgData = ctx.getImageData(0, 0, w, h);
    canvas.width = h;
    canvas.height = w;
    const offCanvas = document.createElement('canvas');
    offCanvas.width = w;
    offCanvas.height = h;
    offCanvas.getContext('2d').putImageData(imgData, 0, 0);
    ctx.translate(canvas.width / 2, canvas.height / 2);
    ctx.rotate(90 * Math.PI / 180);
    ctx.drawImage(offCanvas, -w / 2, -h / 2);
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    if (paintManager) {
        paintManager.clearHistory();
        paintManager.clearElements();
        paintManager.saveToHistory();
    }
}

function clearCanvas() {
    if (!confirm('清除画布内容?')) return false;
    fillCanvas('white');
    if (paintManager) {
        paintManager.clearElements();
        if (cropManager.isCropMode()) cropManager.exitCropMode();
        paintManager.saveToHistory();
    }
    return true;
}

// ==================== 抖动处理（带防抖）====================
let _pendingDitherJob = null;

/**
 * 调整亮度（整体偏移）
 * @param {ImageData} imageData 图像数据
 * @param {number} brightness  亮度系数，范围 0.5～1.5，1.0 表示不变
 * @returns {ImageData} 处理后的图像数据
 */
function adjustBrightness(imageData, brightness) {
    const data = imageData.data;
    const offset = (brightness - 1) * 128;   // 亮度偏移量，范围 -64～+64
    if (Math.abs(offset) < 0.5) return imageData;
    for (let i = 0; i < data.length; i += 4) {
        data[i]     = Math.min(255, Math.max(0, data[i]     + offset));
        data[i + 1] = Math.min(255, Math.max(0, data[i + 1] + offset));
        data[i + 2] = Math.min(255, Math.max(0, data[i + 2] + offset));
    }
    return imageData;
}

function scheduleConvertDithering() {
    if (_pendingDitherJob) {
        if (_pendingDitherJob.type === 'idle' && typeof cancelIdleCallback === 'function') {
            cancelIdleCallback(_pendingDitherJob.id);
        } else if (_pendingDitherJob.type === 'raf') {
            cancelAnimationFrame(_pendingDitherJob.id);
        }
        _pendingDitherJob = null;
    }
    const doDither = () => {
        _pendingDitherJob = null;
        if (typeof convertDithering === 'function') convertDithering();
    };
    if (typeof requestIdleCallback === 'function') {
        _pendingDitherJob = { type: 'idle', id: requestIdleCallback(doDither, { timeout: 200 }) };
    } else {
        _pendingDitherJob = { type: 'raf', id: requestAnimationFrame(doDither) };
    }
}

/**
 * 对 ImageData 执行完整的预处理（对比度、亮度、抖动）
 * @param {ImageData} imageData 
 * @param {Object} options 可选覆盖参数，若不传则从 UI 读取
 * @returns {ImageData} 处理后的 ImageData
 */
function processImageDataWithUI(imageData, options = {}) {
    const contrast = options.contrast !== undefined ? options.contrast : parseFloat(document.getElementById('ditherContrast').value);
    const brightness = options.brightness !== undefined ? options.brightness : parseFloat(document.getElementById('ditherBrightness').value);
    const alg = options.alg || document.getElementById('ditherAlg').value;
    const strength = options.strength !== undefined ? options.strength : parseFloat(document.getElementById('ditherStrength').value);
    const colorMode = options.colorMode || document.getElementById('ditherMode').value;
    const useLegacy = document.getElementById('useLegacyDither')?.checked || false;
    
    let processed = new ImageData(new Uint8ClampedArray(imageData.data), imageData.width, imageData.height);
    if (!isNaN(contrast)) processed = adjustContrast(processed, contrast);
    if (!isNaN(brightness) && brightness !== 1.0) processed = adjustBrightness(processed, brightness);
    if (alg !== 'none') {
        // 注意：ditherImage 会直接修改传入的 imageData，我们传入一个副本
        processed = ditherImage(processed, alg, strength, colorMode);
    }
    return processed;
}
/**
 * 将当前主画布内容保存为指定面的 ImageData
 * @param {'A'|'B'} side 
 */
function syncCurrentCanvasToSide(side) {
    const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
    if (side === 'A') {
        storedImageDataA = imageData;
        document.getElementById('aStatusLabel').innerText = `已同步 (${canvas.width}x${canvas.height})`;
        addLog(`✅ 已将当前画布内容同步到 A 面（尺寸 ${canvas.width}x${canvas.height}）`);
        // 可选：更新预览（如果用户勾选了“实时预览”）
    } else {
        storedImageDataB = imageData;
        document.getElementById('bStatusLabel').innerText = `已同步 (${canvas.width}x${canvas.height})`;
        addLog(`✅ 已将当前画布内容同步到 B 面（尺寸 ${canvas.width}x${canvas.height}）`);
    }
    // 更新底部协议状态栏中的 A/B 面状态
    updateUIBasedOnProtocol();
    // 若处于 AB 异显模式，可自动勾选启用抖动（视觉反馈）
    const abSelect = document.getElementById('abSelect');
    if (abSelect && abSelect.value === '7') {
        document.getElementById('ditherEnable').checked = true;
    }
}

function convertDithering() {
	// 如果是特殊模式（课表、待办等），不进行抖动
    const hasSpecial = paintManager && (
        (paintManager.scheduleData && paintManager.scheduleData.length > 0) ||
        (paintManager.todoData && paintManager.todoData.length > 0) ||
        paintManager.cardData ||
        paintManager.wifiData
    );
    if (hasSpecial) {
        addLog("特殊模式：已禁用抖动/对比度调整（直接发送渲染结果）");
        return;
    }

    if (paintManager) {
        paintManager.redrawTextElements();
        paintManager.redrawLineSegments();
    }

    // 1. 获取当前画布数据
    const current = ctx.getImageData(0, 0, canvas.width, canvas.height);
    let imgData = new ImageData(new Uint8ClampedArray(current.data), current.width, current.height);
    // 2. 亮度调整
    const brightness = parseFloat(document.getElementById('ditherBrightness').value);
    if (!isNaN(brightness) && brightness !== 1.0) {
        imgData = adjustBrightness(imgData, brightness);
    }
    // 3. 对比度调整
    const contrast = parseFloat(document.getElementById('ditherContrast').value);
    if (!isNaN(contrast)) {
        imgData = adjustContrast(imgData, contrast);
    }
    //adjustContrast(imgData, contrast);
    // 4. 抖动处理
    const alg = document.getElementById('ditherAlg').value;
    const strength = parseFloat(document.getElementById('ditherStrength').value);
    const mode = document.getElementById('ditherMode').value;
    const processed = processImageData(ditherImage(imgData, alg, strength, mode), mode);
    const final = decodeProcessedData(processed, canvas.width, canvas.height, mode);
    ctx.putImageData(final, 0, 0);
    if (paintManager) paintManager.saveToHistory();
}

function applyDither() {
    cropManager.finishCrop(() => scheduleConvertDithering());
}

// ==================== 假期同步功能 ====================
async function loadHolidayJson(year) {
    try {
        const resp = await fetch(`holiday-cn/${year}.json`);
        if (resp.ok) {
            const data = await resp.json();
            addLog(`成功加载${year}年假期数据，共${data.days.length}条记录`);
            return data;
        } else {
            addLog(`未找到${year}年的假期数据文件 (HTTP ${resp.status})`);
            addLog(`请确认文件路径: holiday-cn/${year}.json`);
            return null;
        }
    } catch (e) {
        addLog("加载假期数据失败: " + e.message);
        if (e.message.includes('Failed to fetch')) {
            addLog("⚠️ 可能的原因:");
            addLog("  1. 请通过HTTP服务器访问此页面（而非file://协议）");
            addLog(`  2. 检查holiday-cn/${year}.json文件是否存在`);
        }
        return null;
    }
}

function convertJsonToDeviceFormat(holidayJson) {
    const codes = [];
    for (const day of holidayJson.days) {
        const [year, month, date] = day.date.split('-');
        const m = parseInt(month, 10);
        const d = parseInt(date, 10);
        const flag = day.isOffDay ? 0 : 1;
        codes.push((flag << 12) | (m << 8) | d);
    }
    return codes;
}

function validateHolidayJson(obj) {
    if (!obj || typeof obj !== 'object') return { ok: false, message: "JSON 为空或格式错误" };
    if (!Number.isInteger(obj.year) || obj.year < 2000 || obj.year > 2100) return { ok: false, message: "缺少有效的 year 字段" };
    if (!Array.isArray(obj.days)) return { ok: false, message: "缺少 days 数组" };
    for (const day of obj.days) {
        if (!day || typeof day !== 'object') return { ok: false, message: "days 中包含无效项" };
        if (typeof day.date !== 'string' || !/^\d{4}-\d{2}-\d{2}$/.test(day.date)) return { ok: false, message: "days.date 格式错误，应为 YYYY-MM-DD" };
        if (typeof day.isOffDay !== 'boolean') return { ok: false, message: "days.isOffDay 必须为布尔值" };
    }
    return { ok: true };
}

async function importHolidayJsonFile(file) {
    try {
        addLog("正在读取假期文件: " + file.name);
        const text = await file.text();
        const json = JSON.parse(text);
        const valid = validateHolidayJson(json);
        if (valid.ok) {
            const codes = convertJsonToDeviceFormat(json);
            await sendHolidayDataToDevice(json.year, codes, "导入文件: " + file.name);
        } else {
            alert("假期 JSON 校验失败：" + valid.message);
            addLog("❌ 假期 JSON 校验失败：" + valid.message);
        }
    } catch (e) {
        alert("读取或解析假期 JSON 失败：" + e.message);
        addLog("❌ 读取或解析假期 JSON 失败：" + e.message);
    }
}

async function sendHolidayDataToDevice(year, codes, source) {
    if (codes.length === 0) {
        alert("没有有效的假期数据！");
        return;
    }
    if (codes.length > 128) {
        alert("假期数据过多，最多支持128个！");
        return;
    }
    if (!confirm(`即将同步${year}年的假期数据到设备\n共${codes.length}个假期/调休日\n数据来源: ${source}\n\n确认继续？`)) {
        addLog("用户取消了同步操作");
        return;
    }
    const buf = new Uint8Array(3 + codes.length * 2);
    buf[0] = (year >> 8) & 0xFF;
    buf[1] = year & 0xFF;
    buf[2] = codes.length;
    for (let i = 0; i < codes.length; i++) {
        buf[3 + i * 2] = (codes[i] >> 8) & 0xFF;
        buf[3 + i * 2 + 1] = codes[i] & 0xFF;
    }
    addLog(`正在发送 ${year} 年假期数据，共 ${codes.length} 个假期...`);
    const success = await write(EpdCmd.SET_HOLIDAYS, buf);
    if (success) {
        addLog("✅ 假期数据已成功同步到设备！");
        addLog("✔ 数据已保存到设备，断电不丢失");
    } else {
        addLog("❌ 假期数据发送失败！");
    }
}

async function syncHolidayData() {
    const year = new Date().getFullYear();
    addLog(`正在加载${year}年的假期数据...`);
    const json = await loadHolidayJson(year);
    if (json) {
        await sendHolidayDataToDevice(year, convertJsonToDeviceFormat(json), "holiday-cn");
    } else {
        alert(`无法加载${year}年的假期数据！\n请确认 holiday-cn/${year}.json 文件存在。`);
    }
}

function showHolidayHelp() {
    document.getElementById("holidayHelpDialog").style.display = "block";
    document.getElementById("holidayHelpOverlay").style.display = "block";
}
function closeHolidayHelp() {
    document.getElementById("holidayHelpDialog").style.display = "none";
    document.getElementById("holidayHelpOverlay").style.display = "none";
}

async function syncAndShowCalendar() {
    const year = parseInt(document.getElementById("testYear").value);
    const month = parseInt(document.getElementById("testMonth").value);
    const info = document.getElementById("holidayTestInfo");
    if (!year || year < 2007 || year > 2050) {
        alert("请输入有效的年份 (2007-2050)");
        return;
    }
    if (!month || month < 1 || month > 12) {
        alert("请选择有效的月份 (1-12)");
        return;
    }
    addLog(`=== 假期日历测试: ${year}年${month}月 ===`);
    info.textContent = `正在加载 ${year} 年数据...`;
    const json = await loadHolidayJson(year);
    if (!json) {
        info.textContent = `❌ 未找到 ${year} 年数据`;
        alert(`无法加载${year}年的假期数据！\n请确认 holiday-cn/${year}.json 文件存在。`);
        return;
    }
    const codes = convertJsonToDeviceFormat(json);
    if (codes.length === 0) {
        info.textContent = `⚠️ ${year} 年无假期数据`;
        addLog(`警告: ${year}年没有假期数据`);
    } else {
        info.textContent = `✅ 已加载 ${codes.length} 个假期`;
    }
    const buf = new Uint8Array(3 + codes.length * 2);
    buf[0] = (year >> 8) & 0xFF;
    buf[1] = year & 0xFF;
    buf[2] = codes.length;
    for (let i = 0; i < codes.length; i++) {
        buf[3 + i * 2] = (codes[i] >> 8) & 0xFF;
        buf[3 + i * 2 + 1] = codes[i] & 0xFF;
    }
    addLog(`发送 ${year} 年假期数据 (${codes.length} 个)...`);
    if (await write(EpdCmd.SET_HOLIDAYS, buf)) {
        addLog("✅ 假期数据已发送");
        const targetDate = new Date(year, month - 1, 1, 0, 0, 0);
        const timestamp = Math.floor(targetDate.getTime() / 1000);
        addLog(`设置日期到: ${year}-${String(month).padStart(2,'0')}-01`);
        const timeData = new Uint8Array([
            (timestamp >> 24) & 0xFF, (timestamp >> 16) & 0xFF,
            (timestamp >> 8) & 0xFF, timestamp & 0xFF,
            -(new Date().getTimezoneOffset() / 60),
            1
        ]);
        if (await write(EpdCmd.SET_TIME, timeData)) {
            addLog(`✅ 已设置到 ${year}年${month}月，切换到日历模式`);
            addLog(`📅 设备将显示 ${year}年${month}月的日历和调休信息`);
            addLog("⏳ 屏幕刷新完成前请不要操作");
            info.textContent = `✅ 显示 ${year}年${month}月日历`;
            const monthCodes = codes.filter(c => ((c >> 8) & 0x0F) === month);
            if (monthCodes.length > 0) {
                const rest = monthCodes.filter(c => ((c >> 12) & 0x0F) === 0).length;
                const work = monthCodes.filter(c => ((c >> 12) & 0x0F) === 1).length;
                addLog(`📊 ${month}月: ${rest}个休息日, ${work}个调休上班日`);
            } else {
                addLog(`📊 ${month}月: 无特殊假期安排`);
            }
        } else {
            info.textContent = "❌ 切换日历失败";
            addLog("❌ 切换到日历模式失败！");
        }
    } else {
        info.textContent = "❌ 假期数据发送失败";
        addLog("❌ 假期数据发送失败！");
    }
}

// ==================== 测试时间戳功能 ====================
function setCurrentTimestamp() {
    const now = Math.floor(Date.now() / 1000);
    document.getElementById("testTimestamp").value = now;
    updateTimestampInfo();
}
function addDays(delta) {
    const input = document.getElementById("testTimestamp");
    let val = parseInt(input.value);
    if (isNaN(val)) val = Math.floor(Date.now() / 1000);
    input.value = val + delta * 86400;
    updateTimestampInfo();
}
function updateTimestampInfo() {
    const ts = parseInt(document.getElementById("testTimestamp").value);
    const info = document.getElementById("timestampInfo");
    if (isNaN(ts) || ts <= 0) {
        info.textContent = "";
        return;
    }
    const d = new Date(ts * 1000);
    const y = d.getFullYear();
    const m = String(d.getMonth() + 1).padStart(2,'0');
    const day = String(d.getDate()).padStart(2,'0');
    const h = String(d.getHours()).padStart(2,'0');
    const min = String(d.getMinutes()).padStart(2,'0');
    const s = String(d.getSeconds()).padStart(2,'0');
    const week = ["日","一","二","三","四","五","六"][d.getDay()];
    info.textContent = `对应时间: ${y}-${m}-${day} 星期${week} ${h}:${min}:${s}`;
}
async function testCalendarJump(mode) {
    const ts = parseInt(document.getElementById("testTimestamp").value);
    if (isNaN(ts) || ts <= 0) {
        alert("请输入有效的时间戳（Unix时间戳，秒为单位）");
        return;
    }
    const localStr = new Date(ts * 1000).toLocaleString("zh-CN");
    const modeName = mode === 1 ? "日历" : "时钟";
    if (!confirm(`确认要将设备时间设置为:\n${localStr}\n并切换到${modeName}模式?`)) return;
    const data = new Uint8Array([
        (ts >> 24) & 0xFF, (ts >> 16) & 0xFF, (ts >> 8) & 0xFF, ts & 0xFF,
        -(new Date().getTimezoneOffset() / 60),
        mode
    ]);
    if (await write(EpdCmd.SET_TIME, data)) {
        addLog("测试时间已设置: " + localStr);
        addLog(`模式: ${modeName}模式`);
        addLog("屏幕刷新完成前请不要操作。");
    }
}

// ==================== 编辑器初始化（保持不变）====================
function initImageEditor() {
    const imageModeBtn = document.getElementById('image-mode');
    const imagePanel = document.getElementById('image-panel');
    const schedulePanel = document.getElementById('schedule-panel');
    const todoPanel = document.getElementById('todo-panel');
    const cardPanel = document.getElementById('card-panel');
    const wifiPanel = document.getElementById('wifi-panel');
    const scheduleModeBtn = document.getElementById('schedule-mode');
    const todoModeBtn = document.getElementById('todo-mode');
    const cardModeBtn = document.getElementById('card-mode');
    const wifiModeBtn = document.getElementById('wifi-mode');

    if (imageModeBtn && imagePanel) {
        imageModeBtn.addEventListener('click', () => {
            const visible = imagePanel.style.display !== 'none';
            imagePanel.style.display = visible ? 'none' : '';
            imageModeBtn.classList.toggle('active', !visible);
            if (!visible) {
                if (schedulePanel) schedulePanel.style.display = 'none';
                if (todoPanel) todoPanel.style.display = 'none';
                if (cardPanel) cardPanel.style.display = 'none';
                if (wifiPanel) wifiPanel.style.display = 'none';
                if (scheduleModeBtn) scheduleModeBtn.classList.remove('active');
                if (todoModeBtn) todoModeBtn.classList.remove('active');
                if (cardModeBtn) cardModeBtn.classList.remove('active');
                if (wifiModeBtn) wifiModeBtn.classList.remove('active');
                if (paintManager) {
                    paintManager.scheduleData = null;
                    paintManager.todoData = null;
                    paintManager.cardData = null;
                    paintManager.wifiData = null;
                    paintManager.redrawAll();
                }
            }
        });
    }
}

function initScheduleEditor() {
    const scheduleModeBtn = document.getElementById('schedule-mode');
    const schedulePanel = document.getElementById('schedule-panel');
    const imagePanel = document.getElementById('image-panel');
    const todoPanel = document.getElementById('todo-panel');
    const cardPanel = document.getElementById('card-panel');
    const wifiPanel = document.getElementById('wifi-panel');
    const imageModeBtn = document.getElementById('image-mode');
    const todoModeBtn = document.getElementById('todo-mode');
    const cardModeBtn = document.getElementById('card-mode');
    const wifiModeBtn = document.getElementById('wifi-mode');
    const createBtn = document.getElementById('create-schedule-btn');
    const syncBtn = document.getElementById('schedule-sync-btn');
    const clearBtn = document.getElementById('schedule-clear-btn');
    const daysSelect = document.getElementById('schedule-days');
    const classesSelect = document.getElementById('schedule-classes');
    const fontSizeSelect = document.getElementById('schedule-font-size');

    if (scheduleModeBtn && schedulePanel) {
        scheduleModeBtn.addEventListener('click', () => {
            const visible = schedulePanel.style.display !== 'none';
            schedulePanel.style.display = visible ? 'none' : '';
            scheduleModeBtn.classList.toggle('active', !visible);
            if (!visible) {
                if (imagePanel) imagePanel.style.display = 'none';
                if (todoPanel) todoPanel.style.display = 'none';
                if (cardPanel) cardPanel.style.display = 'none';
                if (wifiPanel) wifiPanel.style.display = 'none';
                if (imageModeBtn) imageModeBtn.classList.remove('active');
                if (todoModeBtn) todoModeBtn.classList.remove('active');
                if (cardModeBtn) cardModeBtn.classList.remove('active');
                if (wifiModeBtn) wifiModeBtn.classList.remove('active');
                if (paintManager) {
                    paintManager.todoData = null;
                    paintManager.cardData = null;
                    paintManager.wifiData = null;
                    paintManager.redrawAll();
                }
            } else {
                if (typeof renderScheduleEditorTable === 'function') renderScheduleEditorTable();
            }
        });
    }

    if (createBtn) createBtn.addEventListener('click', async () => {
        const editor = document.getElementById('schedule-editor');
        if (editor) editor.style.display = 'none';
        const imagePanel = document.getElementById('image-panel');
        if (imagePanel) imagePanel.style.display = 'none';
        const imageModeBtn = document.getElementById('image-mode');
        if (imageModeBtn) imageModeBtn.classList.remove('active');
        if (paintManager) await paintManager.createSchedule();
        if (typeof renderScheduleEditorTable === 'function') renderScheduleEditorTable();
    });
    if (syncBtn) syncBtn.addEventListener('click', async () => {
        if (typeof syncScheduleToEPD === 'function') await syncScheduleToEPD();
    });
    if (clearBtn) clearBtn.addEventListener('click', () => {
        if (typeof clearScheduleEditorInputs === 'function') clearScheduleEditorInputs();
        if (paintManager && paintManager.scheduleData && paintManager.scheduleData.length) {
            for (let i = 1; i < paintManager.scheduleData.length; i++) {
                for (let j = 1; j < paintManager.scheduleData[i].length; j++) {
                    paintManager.scheduleData[i][j] = "";
                }
            }
            paintManager.redrawAll();
            paintManager.saveToHistory();
        }
    });
    const update = () => { if (typeof renderScheduleEditorTable === 'function') renderScheduleEditorTable(); };
    if (daysSelect) daysSelect.addEventListener('change', update);
    if (classesSelect) classesSelect.addEventListener('change', update);
    if (fontSizeSelect) fontSizeSelect.addEventListener('change', update);
}

function initTodoEditor() {
    const todoModeBtn = document.getElementById('todo-mode');
    const todoPanel = document.getElementById('todo-panel');
    const cardPanel = document.getElementById('card-panel');
    const wifiPanel = document.getElementById('wifi-panel');
    const schedulePanel = document.getElementById('schedule-panel');
    const imagePanel = document.getElementById('image-panel');
    const imageModeBtn = document.getElementById('image-mode');
    const scheduleModeBtn = document.getElementById('schedule-mode');
    const cardModeBtn = document.getElementById('card-mode');
    const wifiModeBtn = document.getElementById('wifi-mode');
    const createBtn = document.getElementById('create-todo-btn');
    const syncBtn = document.getElementById('todo-sync-btn');
    const clearBtn = document.getElementById('todo-clear-btn');
    const countSelect = document.getElementById('todo-count');
    const fontSizeSelect = document.getElementById('todo-font-size');

    if (todoModeBtn && todoPanel) {
        todoModeBtn.addEventListener('click', () => {
            const visible = todoPanel.style.display !== 'none';
            todoPanel.style.display = visible ? 'none' : '';
            todoModeBtn.classList.toggle('active', !visible);
            if (!visible) {
                if (cardPanel) cardPanel.style.display = 'none';
                if (wifiPanel) wifiPanel.style.display = 'none';
                if (schedulePanel) schedulePanel.style.display = 'none';
                if (imagePanel) imagePanel.style.display = 'none';
                if (cardModeBtn) cardModeBtn.classList.remove('active');
                if (wifiModeBtn) wifiModeBtn.classList.remove('active');
                if (scheduleModeBtn) scheduleModeBtn.classList.remove('active');
                if (imageModeBtn) imageModeBtn.classList.remove('active');
                if (paintManager) {
                    paintManager.scheduleData = null;
                    paintManager.cardData = null;
                    paintManager.wifiData = null;
                    paintManager.redrawAll();
                }
            } else {
                if (typeof renderTodoEditorTable === 'function') renderTodoEditorTable();
            }
        });
    }

    if (createBtn) createBtn.addEventListener('click', async () => {
        const editor = document.getElementById('todo-editor');
        if (editor) editor.style.display = 'none';
        const imagePanel = document.getElementById('image-panel');
        if (imagePanel) imagePanel.style.display = 'none';
        const imageModeBtn = document.getElementById('image-mode');
        if (imageModeBtn) imageModeBtn.classList.remove('active');
        if (paintManager) await paintManager.createTodoList();
        if (typeof renderTodoEditorTable === 'function') renderTodoEditorTable();
    });
    if (syncBtn) syncBtn.addEventListener('click', async () => {
        if (typeof syncTodoToEPD === 'function') await syncTodoToEPD();
    });
    if (clearBtn) clearBtn.addEventListener('click', () => {
        if (typeof clearTodoEditorInputs === 'function') clearTodoEditorInputs();
        if (paintManager && paintManager.todoData && paintManager.todoData.length) {
            paintManager.todoData.forEach(item => { item.text = ""; item.done = false; });
            paintManager.redrawAll();
            paintManager.saveToHistory();
        }
    });
    const update = () => { if (typeof renderTodoEditorTable === 'function') renderTodoEditorTable(); };
    if (countSelect) countSelect.addEventListener('change', update);
    if (fontSizeSelect) fontSizeSelect.addEventListener('change', update);
}

function initCardEditor() {
    const cardModeBtn = document.getElementById('card-mode');
    const cardPanel = document.getElementById('card-panel');
    const wifiPanel = document.getElementById('wifi-panel');
    const todoPanel = document.getElementById('todo-panel');
    const schedulePanel = document.getElementById('schedule-panel');
    const imagePanel = document.getElementById('image-panel');
    const imageModeBtn = document.getElementById('image-mode');
    const scheduleModeBtn = document.getElementById('schedule-mode');
    const todoModeBtn = document.getElementById('todo-mode');
    const wifiModeBtn = document.getElementById('wifi-mode');
    const syncBtn = document.getElementById('card-sync-btn');
    const clearBtn = document.getElementById('card-clear-btn');
    const inputs = ['card-name', 'card-title', 'card-phone', 'card-email', 'card-website', 'card-footer'];

    if (cardModeBtn && cardPanel) {
        cardModeBtn.addEventListener('click', () => {
            const visible = cardPanel.style.display !== 'none';
            cardPanel.style.display = visible ? 'none' : '';
            cardModeBtn.classList.toggle('active', !visible);
            if (!visible) {
                if (wifiPanel) wifiPanel.style.display = 'none';
                if (todoPanel) todoPanel.style.display = 'none';
                if (schedulePanel) schedulePanel.style.display = 'none';
                if (imagePanel) imagePanel.style.display = 'none';
                if (wifiModeBtn) wifiModeBtn.classList.remove('active');
                if (todoModeBtn) todoModeBtn.classList.remove('active');
                if (scheduleModeBtn) scheduleModeBtn.classList.remove('active');
                if (imageModeBtn) imageModeBtn.classList.remove('active');
                if (paintManager) {
                    paintManager.scheduleData = null;
                    paintManager.todoData = null;
                    paintManager.wifiData = null;
                    paintManager.redrawAll();
                }
            } else {
                if (typeof updateCardCanvasPreview === 'function') updateCardCanvasPreview({ saveHistory: false, onlyWhenPanelVisible: true });
            }
        });
    }

    if (syncBtn) syncBtn.addEventListener('click', async () => {
        if (typeof syncCardToEPD === 'function') await syncCardToEPD();
    });
    if (clearBtn) clearBtn.addEventListener('click', () => {
        inputs.forEach(id => {
            const el = document.getElementById(id);
            if (el) el.value = '';
        });
        if (paintManager && paintManager.cardData) {
            paintManager.cardData = null;
            paintManager.redrawAll();
            paintManager.saveToHistory();
        }
        if (typeof updateCardCanvasPreview === 'function') updateCardCanvasPreview({ saveHistory: false, onlyWhenPanelVisible: true });
    });
    inputs.forEach(id => {
        const el = document.getElementById(id);
        if (el) el.addEventListener('input', () => {
            if (typeof scheduleCardCanvasPreviewUpdate === 'function') scheduleCardCanvasPreviewUpdate();
        });
    });
    if (typeof scheduleCardCanvasPreviewUpdate === 'function') scheduleCardCanvasPreviewUpdate();
}

function initWifiEditor() {
    const wifiModeBtn = document.getElementById('wifi-mode');
    const wifiPanel = document.getElementById('wifi-panel');
    const todoPanel = document.getElementById('todo-panel');
    const cardPanel = document.getElementById('card-panel');
    const schedulePanel = document.getElementById('schedule-panel');
    const imagePanel = document.getElementById('image-panel');
    const imageModeBtn = document.getElementById('image-mode');
    const scheduleModeBtn = document.getElementById('schedule-mode');
    const todoModeBtn = document.getElementById('todo-mode');
    const cardModeBtn = document.getElementById('card-mode');
    const syncBtn = document.getElementById('wifi-sync');
    const clearBtn = document.getElementById('wifi-clear');
    const ssidInput = document.getElementById('wifi-ssid');
    const passInput = document.getElementById('wifi-password');
    const encSelect = document.getElementById('wifi-encryption');
    const hiddenCheck = document.getElementById('wifi-hidden');

    if (wifiModeBtn && wifiPanel) {
        wifiModeBtn.addEventListener('click', () => {
            const visible = wifiPanel.style.display !== 'none';
            wifiPanel.style.display = visible ? 'none' : '';
            wifiModeBtn.classList.toggle('active', !visible);
            if (!visible) {
                if (todoPanel) todoPanel.style.display = 'none';
                if (cardPanel) cardPanel.style.display = 'none';
                if (schedulePanel) schedulePanel.style.display = 'none';
                if (imagePanel) imagePanel.style.display = 'none';
                if (todoModeBtn) todoModeBtn.classList.remove('active');
                if (cardModeBtn) cardModeBtn.classList.remove('active');
                if (scheduleModeBtn) scheduleModeBtn.classList.remove('active');
                if (imageModeBtn) imageModeBtn.classList.remove('active');
                if (paintManager) {
                    paintManager.scheduleData = null;
                    paintManager.todoData = null;
                    paintManager.cardData = null;
                    paintManager.redrawAll();
                }
            } else {
                if (typeof renderWifiQrPreview === 'function') renderWifiQrPreview();
                if (typeof scheduleWifiCanvasPreviewUpdate === 'function') scheduleWifiCanvasPreviewUpdate();
            }
        });
    }
    if (syncBtn) syncBtn.addEventListener('click', async () => {
        if (typeof syncWifiToEPD === 'function') await syncWifiToEPD();
        if (typeof renderWifiQrPreview === 'function') renderWifiQrPreview();
        if (typeof scheduleWifiCanvasPreviewUpdate === 'function') scheduleWifiCanvasPreviewUpdate();
    });
    if (clearBtn) clearBtn.addEventListener('click', () => {
        if (ssidInput) ssidInput.value = '';
        if (passInput) passInput.value = '';
        if (encSelect) encSelect.value = 'WPA';
        if (hiddenCheck) hiddenCheck.checked = false;
        if (paintManager && paintManager.wifiData) {
            paintManager.wifiData = null;
            paintManager.redrawAll();
            paintManager.saveToHistory();
        }
        if (typeof renderWifiQrPreview === 'function') renderWifiQrPreview();
        if (typeof scheduleWifiCanvasPreviewUpdate === 'function') scheduleWifiCanvasPreviewUpdate();
    });

    const updateUI = () => {
        if (typeof renderWifiQrPreview === 'function') renderWifiQrPreview();
        if (typeof scheduleWifiCanvasPreviewUpdate === 'function') scheduleWifiCanvasPreviewUpdate();
    };
    if (ssidInput) ssidInput.addEventListener('input', updateUI);
    if (passInput) passInput.addEventListener('input', updateUI);
    if (encSelect) encSelect.addEventListener('change', updateUI);
    if (hiddenCheck) hiddenCheck.addEventListener('change', updateUI);
    if (typeof renderWifiQrPreview === 'function') renderWifiQrPreview();
    if (typeof scheduleWifiCanvasPreviewUpdate === 'function') scheduleWifiCanvasPreviewUpdate();
}

// 预览单张图片到主画布（应用当前抖动）
async function previewImageOnCanvas(file) {
    const img = await loadImageFromFile(file);
    const offCanvas = document.createElement('canvas');
    offCanvas.width = canvas.width;
    offCanvas.height = canvas.height;
    const offCtx = offCanvas.getContext('2d');
    offCtx.drawImage(img, 0, 0, canvas.width, canvas.height);
    let imageData = offCtx.getImageData(0, 0, canvas.width, canvas.height);
    
    if (document.getElementById('ditherEnable')?.checked) {
        const alg = document.getElementById('ditherAlg').value;
        const strength = parseFloat(document.getElementById('ditherStrength').value);
        const contrast = parseFloat(document.getElementById('ditherContrast').value);
        const mode = document.getElementById('ditherMode').value;
        let temp = new ImageData(new Uint8ClampedArray(imageData.data), canvas.width, canvas.height);
        temp = adjustContrast(temp, contrast);
        temp = ditherImage(temp, alg, strength, mode);
        imageData.data.set(temp.data);
    }
    ctx.putImageData(imageData, 0, 0);
    if (paintManager) paintManager.saveToHistory();
    addLog(`预览完成（尺寸：${canvas.width}x${canvas.height}）`);
}

// 合并预览 A/B 面（左右并排）
async function previewBothOnCanvas(fileA, fileB) {
    const imgA = await loadImageFromFile(fileA);
    const imgB = await loadImageFromFile(fileB);
    const halfWidth = canvas.width / 2;
    const offCanvas = document.createElement('canvas');
    offCanvas.width = canvas.width;
    offCanvas.height = canvas.height;
    const offCtx = offCanvas.getContext('2d');
    offCtx.drawImage(imgA, 0, 0, halfWidth, canvas.height);
    offCtx.drawImage(imgB, halfWidth, 0, halfWidth, canvas.height);
    let imageData = offCtx.getImageData(0, 0, canvas.width, canvas.height);
    
    if (document.getElementById('ditherEnable')?.checked) {
        const alg = document.getElementById('ditherAlg').value;
        const strength = parseFloat(document.getElementById('ditherStrength').value);
        const contrast = parseFloat(document.getElementById('ditherContrast').value);
        const mode = document.getElementById('ditherMode').value;
        let temp = new ImageData(new Uint8ClampedArray(imageData.data), canvas.width, canvas.height);
        temp = adjustContrast(temp, contrast);
        temp = ditherImage(temp, alg, strength, mode);
        imageData.data.set(temp.data);
    }
    ctx.putImageData(imageData, 0, 0);
    if (paintManager) paintManager.saveToHistory();
    addLog("合并预览完成（左A右B）");
}


/**
 * 驱动预设配置模块
 * 说明：通过选择不同的预设，动态更新 epddriver 下拉列表，
 *      并自动触发 updateDitcherOptions() 更新颜色模式/画布尺寸。
 *      方便后续添加其他驱动配置，只需在 DRIVER_PRESETS 数组中增加一项即可。
 */

// 预设驱动选项模板（HTML 字符串）
const DRIVER_PRESETS = [
    {
        id: "dongshan",
        name: "东山驱动",
        // 选项HTML（与原 QuDong_DongShan 完全一致）
        optionsHtml: `
                    <option value="1D" data-color="blackWhiteColor" data-size="1.54_152_152">1.54寸 (黑白低分, UC8176)</option>
                    <option value="23" data-color="fourColor" data-size="1.54_200_200">1.54寸 (四色, 26P)</option>
                    <option value="17" data-color="threeColor" data-size="1.54_200_200">1.54寸 (三色, UC8176)</option>
                    <option value="18" data-color="blackWhiteColor" data-size="2.13_104_212">2.13寸低分(黑白, SSD1619)</option>
                    <option value="19" data-color="threeColor" data-size="2.13_104_212">2.13寸低分(三色, SSD1619)</option>
                    <option value="0e" data-color="blackWhiteColor" data-size="2.13_128_250">2.13寸 (黑白, SSD1619)</option>
                    <option value="0f" data-color="threeColor" data-size="2.13_128_250">2.13寸 (三色, SSD1619)</option>
                    <option value="13" data-color="fourColor" data-size="2.8_152_296">2.8寸 (四色, JD79668)</option>
                    <option value="11" data-color="blackWhiteColor" data-size="2.9_128_296">2.9寸 (黑白, SSD1619)</option>
                    <option value="12" data-color="threeColor" data-size="2.9_128_296">2.9寸 (三色, SSD1619)</option>
                    <option value="1B" data-color="blackWhiteColor" data-size="2.9_128_296">2.9寸 (黑白, SSD1680)</option>
                    <option value="10" data-color="fourColor" data-size="3.1_300_300">3.1寸 (四色, JD79665)</option>
                    <option value="18" data-color="threeColor" data-size="3.7_240_416">3.7寸 (三色, AI智屏壳)</option>
                    <option value="1C" data-color="fourColor" data-size="3.97_800_480">3.97寸 (四色, 方角四色屏)</option>
                    <option value="14" data-color="fourColor" data-size="3.98_768_552">3.98寸 (四色, 华为手机壳A0)</option>
                    <option value="15" data-color="fourColor" data-size="3.98_768_552">3.98寸 (四色, 华为手机壳A1)</option>
                    <option value="01" data-color="blackWhiteColor" data-size="4.2_400_300">4.2寸 (黑白, UC8176)</option>
                    <option value="02" data-color="threeColor" data-size="4.2_400_300" selected>4.2寸 (三色, UC8176)</option>
                    <option value="16" data-color="threeColor" data-size="4.2_400_300">4.2寸 (三色, SES_4.2BWR_GL340)</option>
                    <option value="03" data-color="blackWhiteColor" data-size="4.2_400_300">4.2寸 (黑白, SSD1619)</option>
                    <option value="04" data-color="threeColor" data-size="4.2_400_300">4.2寸 (三色, SSD1619)</option>
                    <option value="05" data-color="fourColor" data-size="4.2_400_300">4.2寸 (四色, JD79668)</option>
                    <option value="1E" data-color="blackWhiteColor" data-size="5.83_648_480">5.83寸 (黑白, JD79583)</option>
                    <option value="0d" data-color="fourColor" data-size="5.83_648_480">5.83寸 (四色, JD79665)</option>
                    <option value="FF" data-color="sevenColor" data-size="7.3E6_800_480">7.3寸 (七色, Spectra 6)</option>
                    <option value="2b" data-color="threeColor" data-size="7.4_800_480">7.4寸 (三色, SES7.4_GU140)</option>
                    <option value="06" data-color="blackWhiteColor" data-size="7.5_800_480">7.5寸 (黑白, UC8179)</option>
                    <option value="07" data-color="threeColor" data-size="7.5_800_480">7.5寸 (三色, UC8179)</option>
                    <option value="0c" data-color="fourColor" data-size="7.5_800_480">7.5寸 (四色, JD79665)</option>
                    <option value="08" data-color="blackWhiteColor" data-size="7.5_640_384">7.5寸低分 (黑白, UC8159)</option>
                    <option value="09" data-color="threeColor" data-size="7.5_640_384">7.5寸低分 (三色, UC8159)</option>
                    <option value="0a" data-color="blackWhiteColor" data-size="7.5_880_528">7.5寸HD (黑白, SSD1677)</option>
                    <option value="0b" data-color="threeColor" data-size="7.5_880_528">7.5寸HD (三色, SSD1677)</option>
        `
    },
    {
        id: "tsl0922",
        name: "TSL0922驱动",
        // 选项HTML（与原 QuDong_Tsl0922 完全一致）
        optionsHtml: `
                    <option value="13" data-color="fourColor" data-size="3.98_768_552">3.98寸A0 (四色, JD79665)</option>
                    <option value="14" data-color="fourColor" data-size="3.98_768_552">3.98寸A1 (四色, JD79665)</option>
                    <option value="01" data-color="blackWhiteColor" data-size="4.2_400_300">4.2寸 (黑白, UC8176)</option>
                    <option value="03" data-color="threeColor" data-size="4.2_400_300"  selected>4.2寸 (三色, UC8176)</option>
                    <option value="04" data-color="blackWhiteColor" data-size="4.2_400_300">4.2寸 (黑白, SSD1619)</option>
                    <option value="02" data-color="threeColor" data-size="4.2_400_300">4.2寸 (三色, SSD1619)</option>
                    <option value="17" data-color="threeColor" data-size="4.2_400_300">4.2寸 (黑白, SSD1683)</option>
                    <option value="16" data-color="threeColor" data-size="4.2_400_300">4.2寸 (三色, SSD1683)</option>
                    <option value="05" data-color="fourColor" data-size="4.2_400_300">4.2寸 (四色, JD79668)</option>
                    <option value="28" data-color="blackWhiteColor" data-size="5.81_720_256">5.81寸 (黑白, 龙亭)</option>
                    <option value="29" data-color="threeColor" data-size="5.81_720_256">5.81寸 (三色, 龙亭)</option>
                    <option value="19" data-color="blackWhiteColor" data-size="5.83_648_480">5.83寸 (黑白, UC8179)</option>
                    <option value="18" data-color="threeColor" data-size="5.83_648_480">5.83寸 (三色, UC8179)</option>
                    <option value="0f" data-color="blackWhiteColor" data-size="5.83_648_480">5.83寸 (黑白, JD79686)</option>
                    <option value="0e" data-color="threeColor" data-size="5.83_648_480">5.83寸 (三色, JD79686)</option>
                    <option value="0d" data-color="fourColor" data-size="5.83_648_480">5.83寸 (四色, JD79665)</option>
                    <option value="2a" data-color="blackWhiteColor" data-size="7.4_800_480">7.4寸 (黑白, 龙亭)</option>
                    <option value="2b" data-color="threeColor" data-size="7.4_800_480">7.4寸 (三色, 龙亭)</option>
                    <option value="06" data-color="blackWhiteColor" data-size="7.5_800_480">7.5寸 (黑白, UC8179)</option>
                    <option value="07" data-color="threeColor" data-size="7.5_800_480">7.5寸 (三色, UC8179)</option>
                    <option value="0c" data-color="fourColor" data-size="7.5_800_480">7.5寸 (四色, JD79665)</option>
                    <option value="08" data-color="blackWhiteColor" data-size="7.5_640_384">7.5寸低分 (黑白, UC8159)</option>
                    <option value="09" data-color="threeColor" data-size="7.5_640_384">7.5寸低分 (三色, UC8159)</option>
                    <option value="0a" data-color="blackWhiteColor" data-size="7.5_880_528">7.5寸HD (黑白, SSD1677)</option>
                    <option value="0b" data-color="threeColor" data-size="7.5_880_528">7.5寸HD (三色, SSD1677)</option>
                    <option value="11" data-color="threeColor" data-size="10.2_960_640">10.2寸 (三色, SSD1677)</option>
                    <option value="12" data-color="blackWhiteColor" data-size="10.2_960_640">10.2寸 (黑白, SSD1677)</option>
                    <option value="10" data-color="fourColor" data-size="10.2_960_640">10.2寸 (四色, SSD2677)</option>
                    <option value="15" data-color="sixColor" data-size="7.3E6_800_480">7.3寸 (六色, Spectra 6)</option>
        `
    }, 
    // 👇 后续如需添加其他驱动预设，只需在此继续增加对象即可
    {
        id: "260OnlyOne",
        name: "2.66寸52810专用",
        optionsHtml: `
                    <option value="01" data-color="threeColor" data-size="2.66_152_296" selected>2.66寸 (三色, SSD1680)</option>
        `
    }, 
    {
        id: "breeze4dev",
        name: "breeze4dev驱动",
        optionsHtml: `
						<option value="01" data-color="blackWhiteColor" data-size="4.2_400_300">4.2寸 黑白 (UC8176)</option>
						<option value="02" data-color="threeColor" data-size="4.2_400_300">4.2寸 BWR (SSD1619)</option>
						<option value="03" data-color="threeColor" data-size="4.2_400_300">4.2寸 BWR (UC8176)</option>
						<option value="06" data-color="threeColor" data-size="2.13_250_122">2.13寸 三色 (SSD16xx)</option>
						<option value="08" data-color="threeColor" data-size="2.13_250_122" selected>2.13寸 三色 CE (SSD16xx)</option>
						<option value="07" data-color="threeColor" data-size="2.9_296_128">2.9寸 三色 (SSD16xx)</option>
						<option value="09" data-color="threeColor" data-size="2.9_296_128">2.9寸 三色 CE (SSD16xx)</option>
						<option value="0a" data-color="blackWhiteColor" data-size="2.13_212_104">2.13寸 黑白 (SSD16xx)</option>
						<option value="0b" data-color="blackWhiteColor" data-size="2.13_212_104">2.13寸 黑白 CE (SSD16xx)</option>
        `
    }
];

/**
 * 根据预设ID更新 epddriver 的下拉选项
 * @param {string} presetId 预设ID（对应 DRIVER_PRESETS 中的 id）
 */
function applyDriverPreset(presetId) {
    const preset = DRIVER_PRESETS.find(p => p.id === presetId);
    if (!preset) return;
    const epddriver = document.getElementById("epddriver");
    if (!epddriver) return;
    // 使用 innerHTML 动态生成驱动选项（保留原有方式）
    epddriver.innerHTML = preset.optionsHtml;
    // 触发 updateDitcherOptions() 同步颜色模式、画布尺寸等
    if (typeof updateDitcherOptions === 'function') {
        updateDitcherOptions();
    }
    // 可选：手动派发 change 事件以保证外部监听
    epddriver.dispatchEvent(new Event('change'));
}

// 初始化驱动预设下拉选择器
function initDriverPresetSelector() {
    const container = document.getElementById("driverPreset");
    if (!container) return;
    // 构建选项
    DRIVER_PRESETS.forEach(preset => {
        const option = document.createElement("option");
        option.value = preset.id;
        option.textContent = preset.name;
        container.appendChild(option);
    });
    // 默认选中“东山驱动”
    container.value = "dongshan";
    // 监听变化
    container.addEventListener("change", (e) => {
        applyDriverPreset(e.target.value);
    });
    // 立即应用默认预设
    applyDriverPreset("dongshan");
}
/*
// 页面加载完成后初始化
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initDriverPresetSelector);
} else {
    initDriverPresetSelector();
}
*/


// ==================== 主入口 ====================
document.body.onload = () => {
    textDecoder = null;
    canvas = document.getElementById('canvas');
    ctx = canvas.getContext("2d", { willReadFrequently: true });
    ctx.fillStyle = 'white';
    ctx.fillRect(0, 0, canvas.width, canvas.height);

    paintManager = new PaintManager(canvas, ctx);
    cropManager = new CropManager(canvas, ctx, paintManager);
    paintManager.initPaintTools();
    cropManager.initCropTools();

    initEventHandlers();
    updateButtonStatus();
    checkDebugMode();

    const initEditors = () => {
        initImageEditor();
        initScheduleEditor();
        initTodoEditor();
        initCardEditor();
        initWifiEditor();
    };
    if (typeof requestIdleCallback === 'function') {
        requestIdleCallback(initEditors, { timeout: 200 });
    } else {
        setTimeout(initEditors, 0);
    }
};

// 事件初始化（包含新增亮度滑块和同步时间按钮的监听）
function initEventHandlers() {
    document.getElementById("ditherStrength").addEventListener("input", (e) => {
        document.getElementById("ditherStrengthValue").innerText = parseFloat(e.target.value).toFixed(1);
        applyDither();
    });
    document.getElementById("ditherContrast").addEventListener("input", (e) => {
        document.getElementById("ditherContrastValue").innerText = parseFloat(e.target.value).toFixed(1);
        applyDither();
    });
    // 新增亮度滑块
    const brightnessSlider = document.getElementById('ditherBrightness');
    const brightnessValue = document.getElementById('ditherBrightnessValue');
    if (brightnessSlider && brightnessValue) {
        brightnessSlider.addEventListener('input', (e) => {
            ditherBrightness = parseFloat(e.target.value);
            brightnessValue.innerText = ditherBrightness.toFixed(1);
            applyDither();
        });
    }
    const importBtn = document.getElementById("importholidaybutton");
    const holidayFile = document.getElementById("holidayJsonFile");
    if (importBtn && holidayFile) {
        importBtn.addEventListener('click', () => holidayFile.click());
        holidayFile.addEventListener('change', (e) => {
            const file = e.target.files[0];
            if (file) {
                importHolidayJsonFile(file);
                holidayFile.value = '';
            }
        });
    }
    // 新增“同步时间”按钮监听
    const syncTimeBtn = document.getElementById("syncTimeOnlyBtn");
    if (syncTimeBtn) {
        syncTimeBtn.addEventListener('click', () => syncTimeOnly());
    }
    //新增"下载图片"按钮监听
    const downloadImgBtn = document.getElementById('download-image-btn');
    if (downloadImgBtn) {
        downloadImgBtn.addEventListener('click', downloadImage);
    }
    
    // ---------- A/B面预览功能 ----------
    const previewABtn = document.getElementById('previewA');
    const previewBBtn = document.getElementById('previewB');
    const syncBothBtn = document.getElementById('syncBothToCanvas');

    if (previewABtn) {
        previewABtn.addEventListener('click', async () => {
            const file = document.getElementById('imageFileA')?.files[0];
            if (!file) { addLog("请先选择A面图片"); return; }
            await previewImageOnCanvas(file);
        });
    }
    if (previewBBtn) {
        previewBBtn.addEventListener('click', async () => {
            const file = document.getElementById('imageFileB')?.files[0];
            if (!file) { addLog("请先选择B面图片"); return; }
            await previewImageOnCanvas(file);
        });
    }
    if (syncBothBtn) {
        syncBothBtn.addEventListener('click', async () => {
            const fileA = document.getElementById('imageFileA')?.files[0];
            const fileB = document.getElementById('imageFileB')?.files[0];
            if (!fileA || !fileB) { addLog("请同时选择A面和B面图片"); return; }
            await previewBothOnCanvas(fileA, fileB);
        });
    }
    initDriverPresetSelector();//初始化驱动预设配置模块
    document.addEventListener('visibilitychange', () => {
        if (!document.hidden) {
            // 重新检查协议UI，强制重绘工具栏
            updateUIBasedOnProtocol();
            // 若当前有画板，重绘一次确保光标等正常
            if (paintManager) paintManager.redrawAll();
        }
    });
    // 在 initEventHandlers 末尾添加
    const syncABtn = document.getElementById('syncToABtn');
    const syncBBtn = document.getElementById('syncToBBtn');
    if (syncABtn) syncABtn.addEventListener('click', () => syncCurrentCanvasToSide('A'));
    if (syncBBtn) syncBBtn.addEventListener('click', () => syncCurrentCanvasToSide('B'));
}

function checkDebugMode() {
    const link = document.getElementById('debug-toggle');
    const debug = new URLSearchParams(window.location.search).get('debug') === 'true';
    if (debug) {
        document.body.classList.add('dark-mode');
        if (link) {
            link.innerHTML = '正常模式';
            link.setAttribute('href', window.location.pathname);
        }
        addLog("注意：开发模式功能已开启！不懂请不要随意修改，否则后果自负！");
    } else {
        document.body.classList.remove('dark-mode');
        if (link) {
            link.innerHTML = '开发模式';
            link.setAttribute('href', window.location.pathname + '?debug=true');
        }
    }
}
