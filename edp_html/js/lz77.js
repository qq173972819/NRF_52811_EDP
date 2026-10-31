/**
 * LZ77 压缩算法（移植自安卓APP）
 * 参数：MAX_WND_SIZE = 1024, OFFSET_CODING_LENGTH = 10, m = 3
 * 输出：压缩后的字节数组，以及位长度（ulNumberOfBits）
 */

class LZ77Compress {
    constructor() {
        this.MAX_WND_SIZE = 1024;
        this.OFFSET_CODING_LENGTH = 10;
        this.m = 3;
        this.ulNumberOfBits = 0;
        this.pDataBuffer = null;
        this.pOutputBuffer = null;
        this.pSlideWindowPtr = 0;
        this.pUnprocessedDataPtr = 0;
        this.offset = 0;
        this.length = 0;
    }

    // 比较两个字符串的最大匹配长度
    compareStrings(string1, string2, maxLength) {
        let p1 = string1, p2 = string2;
        let i = 0;
        while (i < maxLength && this.pDataBuffer[p1] === this.pDataBuffer[p2]) {
            p1++;
            p2++;
            i++;
        }
        return i;
    }

    // 查找最长子串
    findLongestSubstring(pSourceString, pString, ulSourceStringLength) {
        let ioffset = 0;
        let pulSubstringOffset = 0;
        let pulSubstringLength = 0;
        let pSrc = pSourceString;
        let ulMaxLength = ulSourceStringLength;
        for (let i = 0; i < ulMaxLength; i++) {
            const len = this.compareStrings(pSrc, pString, ulMaxLength - i);
            if (len > pulSubstringLength) {
                pulSubstringLength = len;
                pulSubstringOffset = ioffset;
            }
            pSrc++;
            ioffset++;
        }
        this.offset = pulSubstringOffset;
        this.length = pulSubstringLength;
    }

    // 写入bit
    write0ToBitStream(ulBitOffset) {
        const ulByteBoundary = ulBitOffset >> 3;
        const ulOffsetInByte = ulBitOffset & 7;
        let c = this.pOutputBuffer[ulByteBoundary];
        c &= ~(1 << ulOffsetInByte);
        this.pOutputBuffer[ulByteBoundary] = c;
    }
    write1ToBitStream(ulBitOffset) {
        const ulByteBoundary = ulBitOffset >> 3;
        const ulOffsetInByte = ulBitOffset & 7;
        let c = this.pOutputBuffer[ulByteBoundary];
        c |= (1 << ulOffsetInByte);
        this.pOutputBuffer[ulByteBoundary] = c;
    }

    // 写Golomb编码长度
    writeGolombCode(ulBitOffset) {
        let x = this.length;
        const q = (x - 1) >> this.m;
        const r = (x - 1) - (q << this.m);
        for (let i = 0; i < q; i++) {
            this.write1ToBitStream(ulBitOffset);
            ulBitOffset++;
        }
        this.write0ToBitStream(ulBitOffset);
        ulBitOffset++;
        for (let i = 0; i < this.m; i++) {
            if ((r >> i) & 1) this.write1ToBitStream(ulBitOffset);
            else this.write0ToBitStream(ulBitOffset);
            ulBitOffset++;
        }
        return this.m + q + 1;
    }

    // 压缩入口
    compress(bytes) {
        this.pDataBuffer = bytes;
        const ulDataLength = bytes.length;
        this.pOutputBuffer = new Uint8Array(ulDataLength * 2);
        let iSlideWindowPtr = -this.MAX_WND_SIZE;
        let ulBitOffset = 0;
        let ulBytesCoded = 0;

        while (ulBytesCoded < ulDataLength) {
            let ulMaxlength = 0;
            if (iSlideWindowPtr >= 0) {
                this.pSlideWindowPtr = iSlideWindowPtr;
                ulMaxlength = this.MAX_WND_SIZE;
            } else if (iSlideWindowPtr >= -this.MAX_WND_SIZE) {
                this.pSlideWindowPtr = 0;
                ulMaxlength = this.MAX_WND_SIZE + iSlideWindowPtr;
            } else {
                this.pSlideWindowPtr = -1;
                ulMaxlength = 0;
            }

            this.pUnprocessedDataPtr = ulBytesCoded;
            const remain = ulDataLength - ulBytesCoded;
            if (ulMaxlength > remain) ulMaxlength = remain;

            if (this.pSlideWindowPtr !== -1 && ulMaxlength > 0) {
                this.findLongestSubstring(this.pSlideWindowPtr, this.pUnprocessedDataPtr, ulMaxlength);
            } else {
                this.length = 0;
            }

            if (this.length > 1) {
                // 匹配成功，输出1位+偏移量+长度
                this.write1ToBitStream(ulBitOffset);
                ulBitOffset++;
                // 写入偏移量（OFFSET_CODING_LENGTH位）
                for (let i = 0; i < this.OFFSET_CODING_LENGTH; i++) {
                    if ((this.offset >> i) & 1) this.write1ToBitStream(ulBitOffset);
                    else this.write0ToBitStream(ulBitOffset);
                    ulBitOffset++;
                }
                // 写入长度（Golomb码）
                const codedLen = this.writeGolombCode(ulBitOffset);
                ulBitOffset += codedLen;
                iSlideWindowPtr += this.length;
                ulBytesCoded += this.length;
            } else {
                // 不匹配，输出0位+原始字节
                this.write0ToBitStream(ulBitOffset);
                ulBitOffset++;
                const c = this.pDataBuffer[ulBytesCoded];
                for (let i = 0; i < 8; i++) {
                    if ((c >> i) & 1) this.write1ToBitStream(ulBitOffset);
                    else this.write0ToBitStream(ulBitOffset);
                    ulBitOffset++;
                }
                iSlideWindowPtr++;
                ulBytesCoded++;
            }
        }

        const k = Math.ceil(ulBitOffset / 8);
        const compressed = new Uint8Array(k);
        for (let i = 0; i < k; i++) compressed[i] = this.pOutputBuffer[i];
        this.ulNumberOfBits = ulBitOffset;
        return compressed;
    }

    getUlNumberOfBits() {
        return this.ulNumberOfBits;
    }
}

// 导出为全局
if (typeof window !== 'undefined') window.LZ77Compress = LZ77Compress;