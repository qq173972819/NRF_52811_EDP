/**
 * APP 协议模块 – 严格模拟 Android APP 的发送逻辑
 * 命令通过 0000ff03 发送，图片数据通过 0000ff02 发送
 */

const AppProtocol = (function() {
    const CMD = {
        INIT: 0x00,
        SEND_PIC_INFO: 0x01,
        START_DECOMPRESS: 0xA0,      // NFC 模式解压缩开始命令
        QUERY_BUFFER: 0xA1,          // NFC 模式查询解压状态
        UPDATE: 0x03                 // 刷新命令（带参数）
    };

    let cmdCharacteristic = null;   // 命令特征 (0000ff03)
    let dataCharacteristic = null;  // 数据特征 (0000ff02)
    let epdType = 0x06;
    let epdIndex = 1;
    let compress = false;
    let packetSize = 20;          // 默认负载大小（MTU=23时）
    let onProgressCallback = null;
    let onCompleteCallback = null;
    let externalLog = null;
    
    let notifyCharacteristic = null;
    let decompressCompleted = false;
    let statusPollingTimer = null;

    function setNotifyCharacteristic(notifyCh) {
        notifyCharacteristic = notifyCh;
        if (notifyCharacteristic) {
            notifyCharacteristic.addEventListener('characteristicvaluechanged', (event) => {
                const data = new Uint8Array(event.target.value.buffer);
                // NFC 模式中设备返回 0x90,0x00 表示解压完成
                if (data.length >= 2 && data[0] === 0x90 && data[1] === 0x00) {
                    decompressCompleted = true;
                    log("[APP] Decompress complete notification received (0x90,0x00)");
                } else if (data.length >= 2 && data[0] === 0x80 && data[1] === 0x90) {
                    log("[APP] Decompress in progress... (0x80,0x90)");
                } else if (data.length >= 2 && data[0] === 0x00 && data[1] === 0x00) {
                    log(`[APP] Notify data: ${Array.from(data).map(b => b.toString(16).padStart(2,'0')).join(' ')} 图片上传成功, 刷新完成, 可以进行下一次传图了`);
                } else if (data.length > 0) {
                    log(`[APP] Notify data: ${Array.from(data).map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
                }
            });
        }
    }

    function setCharacteristics(cmdCh, dataCh) {
        cmdCharacteristic = cmdCh;
        dataCharacteristic = dataCh;
    }
    function setEpdType(type) { epdType = type; }
    function setEpdIndex(idx) { epdIndex = idx; }
    function setCompress(enable) { compress = enable; }
    function setMtuSize(mtu) { packetSize = Math.max(20, mtu - 3); }
    function setProgressCallback(cb) { onProgressCallback = cb; }
    function setCompleteCallback(cb) { onCompleteCallback = cb; }
    function setLogCallback(cb) { externalLog = cb; }

    function log(msg) { if (externalLog) externalLog(msg); else console.log(msg); }
    function sleep(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }

    async function sendInit() {
        if (!cmdCharacteristic) throw new Error('Command characteristic not set');
        const cmd = new Uint8Array([CMD.INIT, epdType, epdIndex]);
        await cmdCharacteristic.writeValueWithResponse(cmd);
        log(`[APP] >>> INIT (cmd): ${Array.from(cmd).map(b => b.toString(16).padStart(2,'0')).join(' ')}`);
    }

    async function sendPicInfo(totalBytes, compressType) {
        if (!cmdCharacteristic) throw new Error('Command characteristic not set');
        const cmd = new Uint8Array(6);
        cmd[0] = CMD.SEND_PIC_INFO;
        cmd[1] = compressType;
        cmd[2] = totalBytes & 0xFF;
        cmd[3] = (totalBytes >> 8) & 0xFF;
        cmd[4] = (totalBytes >> 16) & 0xFF;
        cmd[5] = (totalBytes >> 24) & 0xFF;
        await cmdCharacteristic.writeValueWithResponse(cmd);
        log(`[APP] >>> PIC_INFO (cmd): ${Array.from(cmd).map(b => b.toString(16).padStart(2,'0')).join(' ')} (size=${totalBytes})`);
    }
    
    // 合并后的发送函数：无额外协议头尾，仅按 packetSize 分包，每 39 包延时 100ms
    async function sendRawData(picData) {//完美可用了!!!
        if (!dataCharacteristic) throw new Error('Data characteristic not set');
        const totalLen = picData.length;
        const totalPackets = Math.ceil(totalLen / packetSize);
        let pos = 0;
        let packetCount = 0;
        const MAX_PACKET_CNT = 0x27;      // 39 包后延时 100ms
        const PAYLOAD_SIZE = packetSize;  // 使用外部定义的 packetSize（如 MTU-3 等）

        while (pos < totalLen) {
            const remaining = totalLen - pos;
            const payloadSize = Math.min(PAYLOAD_SIZE, remaining);
            // 直接取原始数据切片，不加任何头尾
            const chunk = picData.subarray(pos, pos + payloadSize);
            await dataCharacteristic.writeValueWithoutResponse(chunk);
            packetCount++;
            pos += payloadSize;

            if (onProgressCallback) {
                onProgressCallback(pos, totalLen);
            }

            // 每 39 包延迟 100ms（与 APP 原逻辑一致）
            if (packetCount % MAX_PACKET_CNT === 0 && pos < totalLen) {
                await new Promise(r => setTimeout(r, 100));
            }
            //log(`[APP] Data sent: ${packetCount * PAYLOAD_SIZE} packets x MTU, ${totalLen} bytes`);
        }
        log(`[APP] Data sent: ${totalPackets} packets, ${totalLen} bytes`);
    }

    async function sendFullImage(processedData, mode, epdTypeValue, isCompressed) {
        if (!cmdCharacteristic || !dataCharacteristic) throw new Error('Characteristics not set');

        decompressCompleted = false;
        
        await sendInit();
        await sleep(100);

        let finalData = processedData;
        let compressType = 0;
        let sizeToSend = processedData.length;

        if (isCompressed) {
            const lz77 = new LZ77Compress();
            finalData = lz77.compress(processedData);
            const dataBits = lz77.getUlNumberOfBits();
            compressType = 1;
            sizeToSend = dataBits;
            log(`[APP] Compressed: ${processedData.length} -> ${finalData.length} bytes, bits=${dataBits}`);
        }

        // 3. 发送图片信息
        await sendPicInfo(sizeToSend, compressType);
        await sleep(200);

        // 4. 发送图片数据（分包）
        await sendRawData(finalData);
        await sleep(200);

        if(isCompressed) {
            log('[APP] 图片数据全部传输完成!等待解压(电脑端:30S+, 手机端:0S)后的自动刷新!');
            log('[APP] 等待时长根据设备不同，电脑端图像复杂了等待时间更长，纯白等待时间最短!');
            await sleep(3000);
        }else{
            log('[APP] 图片数据全部传输完成!等待(电脑端:2分钟+, 手机端:0S)自动刷新!');
            log('[APP] 等待时长根据设备不同，电脑端图像复杂了等待时间更长，纯白等待时间最短!');
            await sleep(1000);
        }

        if (onCompleteCallback) onCompleteCallback();
    }

    return {
        setCharacteristics,
        setEpdType,
        setEpdIndex,
        setCompress,
        setMtuSize,
        setProgressCallback,
        setCompleteCallback,
        setNotifyCharacteristic, 
        setLogCallback,
        sendFullImage,
    };
})();
if (typeof window !== 'undefined') window.AppProtocol = AppProtocol;