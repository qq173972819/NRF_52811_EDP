/**
 * EPD 格式转换模块 – 完全兼容 APP 实现
 * 支持 BW 1-bit, E4 1-bit (多种变体), E5 2-bit, NColor 4-bit, 以及新增的七色 4-bit
 */

const EpdFormat = (function() {
    // 调色板映射（与 APP 保持一致）
    const PALETTE = {
        sixColor: [0x00, 0xFF, 0x4C, 0xE2, 0x96, 0x1D],  // 黑,白,红,黄,绿,蓝
        fourColor: [0x00, 0x01, 0x03, 0x02],               // 黑,白,红,黄
        threeColor: [0x00, 0x01, 0x02],                     // 黑,白,红
        sevenColor: [0x00, 0x01, 0x04, 0x05, 0x02, 0x03, 0x06] // 黑,白,红,黄,绿,蓝,橙
    };

    function getPalette(mode) {
        if (mode === 'sevenColor') return PALETTE.sevenColor;
        if (mode === 'sixColor') return PALETTE.sixColor;
        if (mode === 'fourColor') return PALETTE.fourColor;
        if (mode === 'threeColor') return PALETTE.threeColor;
        return [0, 1]; // blackWhite
    }

    // 将 imageData 量化到调色板索引（使用外部的 findClosestColor 函数，需传递）
    function quantizeToIndex(imageData, mode, findClosest) {
        const width = imageData.width, height = imageData.height;
        const data = imageData.data;
        const palette = getPalette(mode);
        // 预构建颜色映射（简化）
        const indexArray = new Uint8Array(width * height);
        for (let i = 0; i < width * height; i++) {
            const r = data[i*4], g = data[i*4+1], b = data[i*4+2];
            // 使用外部传入的 findClosest 函数（由 dithering.js 提供）
            const closest = findClosest(r, g, b, mode);
            indexArray[i] = closest.value;
        }
        return indexArray;
    }

    // ---------- 七色打包函数（4bit/pixel，类似六色但映射关系不同）----------
    function packSevenColor4bit(width, height, indexArray) {
        const dataLength = Math.ceil(width * height / 2);
        const picData = new Uint8Array(dataLength);
        let byteIdx = 0, shift = 4, temp = 0;
        for (let j = 0; j < height; j++) {
            for (let i = 0; i < width; i++) {
                const idx = j * width + i;
                let nibble = indexArray[idx];
                // 确保 nibble 在 0-7 范围内
                if (nibble > 7) nibble = 1; // default white
                if (shift === 4) {
                    temp = nibble << 4;
                    shift = 0;
                } else {
                    temp |= nibble;
                    picData[byteIdx++] = temp;
                    shift = 4;
                }
            }
        }
        if (shift === 0) picData[byteIdx++] = temp;
        return picData;
    }

    // 六色打包函数（与 APP 一致，用于兼容）
    function packNColor4bit(width, height, indexArray) {
        const dataLength = Math.ceil(width * height / 2);
        const picData = new Uint8Array(dataLength);
        let byteIdx = 0, shift = 4, temp = 0;
        const nibbleMap = {0x00:0, 0xFF:1, 0x4C:2, 0xE2:3, 0x96:4, 0x1D:5};
        for (let j = 0; j < height; j++) {
            for (let i = 0; i < width; i++) {
                const idx = j * width + i;
                let nibble = nibbleMap[indexArray[idx]] || 1;
                if (shift === 4) {
                    temp = nibble << 4;
                    shift = 0;
                } else {
                    temp |= nibble;
                    picData[byteIdx++] = temp;
                    shift = 4;
                }
            }
        }
        if (shift === 0) picData[byteIdx++] = temp;
        return picData;
    }

    // ---------- 打包函数（完全对齐 APP）----------
    function packBW1bit(width, height, indexArray) {
        const byteWidth = Math.ceil(width / 8);
        const picData = new Uint8Array(byteWidth * height);
        let byteIdx = 0, bitCnt = 0, temp = 0;
        for (let j = height-1; j >= 0; j--) {
            for (let i = width-1; i >= 0; i--) {
                const idx = j * width + i;
                const isWhite = (indexArray[idx] !== 0);
                temp = (temp << 1) | (isWhite ? 1 : 0);
                bitCnt++;
                if (bitCnt === 8) {
                    picData[byteIdx++] = temp;
                    temp = 0; bitCnt = 0;
                }
            }
            if (bitCnt) {
                temp <<= (8 - bitCnt);
                picData[byteIdx++] = temp;
                temp = 0; bitCnt = 0;
            }
        }
        return picData;
    }

    function packE4_1bit(width, height, indexArray, reverse = false, invert = false) {
        const byteWidth = Math.ceil(width / 8);
        const bwData = new Uint8Array(byteWidth * height);
        const colorData = new Uint8Array(byteWidth * height);
        let bwByte = 0, clByte = 0, bitCnt = 0, bwTemp = 0, clTemp = 0;
        for (let j = (reverse ? 0 : height-1); (reverse ? j<height : j>=0); j += (reverse ? 1 : -1)) {
            for (let i = 0; i < width; i++) {
                const idx = j * width + i;
                const val = indexArray[idx];
                const isBlack = (val === 0x00);
                const isRed = (val === 0x4C);
                const isYellow = (val === 0xE2);
                bwTemp = (bwTemp << 1) | (isBlack ? 0 : 1);
                clTemp = (clTemp << 1) | ((isRed || isYellow) ? 0 : 1);
                bitCnt++;
                if (bitCnt === 8) {
                    bwData[bwByte++] = invert ? ~bwTemp : bwTemp;
                    colorData[clByte++] = invert ? ~clTemp : clTemp;
                    bwTemp = clTemp = 0; bitCnt = 0;
                }
            }
        }
        if (bitCnt) {
            bwTemp <<= (8 - bitCnt);
            clTemp <<= (8 - bitCnt);
            bwData[bwByte++] = invert ? ~bwTemp : bwTemp;
            colorData[clByte++] = invert ? ~clTemp : clTemp;
        }
        const result = new Uint8Array(bwData.length + colorData.length);
        result.set(bwData, 0);
        result.set(colorData, bwData.length);
        return result;
    }

    function packE5_2bit(width, height, indexArray) {
        const dataLength = Math.ceil(width * height / 4);
        const picData = new Uint8Array(dataLength);
        let byteIdx = 0, bitCnt = 0, temp = 0;
        for (let j = 0; j < height; j++) {
            for (let i = 0; i < width; i++) {
                const idx = j * width + i;
                let val = indexArray[idx];
                let nibble = 0;
                if (val === 0x00) nibble = 0;
                else if (val === 0xFF) nibble = 1;
                else if (val === 0x4C) nibble = 3;
                else if (val === 0xE2) nibble = 2;
                else nibble = 1;
                temp = (temp << 2) | nibble;
                bitCnt += 2;
                if (bitCnt === 8) {
                    picData[byteIdx++] = temp;
                    temp = 0; bitCnt = 0;
                }
            }
        }
        if (bitCnt) {
            temp <<= (8 - bitCnt);
            picData[byteIdx++] = temp;
        }
        return picData;
    }

    // 根据 epd_type 选择正确的打包函数
    function convertWithType(epdType, width, height, imageData, findClosestFn) {
        const mode = getColorModeFromType(epdType);
        const indexArray = quantizeToIndex(imageData, mode, findClosestFn);
        if (epdType === 0x06) { // NCOLOR_0730 使用七色打包
            return packSevenColor4bit(width, height, indexArray);
        } else if (epdType === 0x05) { // NCOLOR_0565 使用六色打包
            return packNColor4bit(width, height, indexArray);
        } else if ([0x00,0x02,0x0C,0x0D,0x0E,0x11].includes(epdType)) {
            return packBW1bit(width, height, indexArray);
        } else if ([0x01,0x03,0x07,0x0A,0x0B,0x0F,0x10].includes(epdType)) {
            return packE4_1bit(width, height, indexArray, false, false);
        } else if (epdType === 0x08) {
            return packE4_1bit(width, height, indexArray, true, false);
        } else if (epdType === 0x09) {
            return packE4_1bit(width, height, indexArray, true, true);
        } else if (epdType === 0x04) {
            return packE5_2bit(width, height, indexArray);
        } else {
            return packBW1bit(width, height, indexArray);
        }
    }

    function getColorModeFromType(epdType) {
        if (epdType === 0x06) return 'sevenColor';
        if (epdType === 0x05) return 'sixColor';
        if ([0x04,0x08,0x09,0x0A,0x0B,0x0F,0x10].includes(epdType)) return 'threeColor';
        if ([0x01,0x03,0x07].includes(epdType)) return 'fourColor';
        return 'blackWhiteColor';
    }

    return { convertWithType };
})();

if (typeof window !== 'undefined') window.EpdFormat = EpdFormat;