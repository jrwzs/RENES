#pragma once

#include "cpu.hpp"
#include "ppu.hpp"
#include "control.hpp"

#include <functional>
#include <stdio.h>
#include "type.hpp"
#include <thread>
#include <unistd.h>
#include <chrono>
#include <mutex>

namespace ReNes {

    class Nes {
        
    public:
        
        Nes()
        {
            setDebug(false);
        }
        
        ~Nes()
        {
            // 等待线程退出
            stop();
            
            _runningThread.join();
            
            printf("Nes即将析构\n");
        }
        
        void stop()
        {
            _stoped = true;
        }
        
        // 加载rom
        void loadRom(const uint8_t* rom, size_t length)
        {
            /*
            // iNES格式 https://wiki.nesdev.com/w/index.php/INES
             iNES file format
             An iNES file consists of the following sections, in order:

             1. Header (16 bytes)
             2. Trainer, if present (0 or 512 bytes) // 由Flags6中的第2bit指出
             3. PRG ROM data (16384 * x bytes)
             4. CHR ROM data, if present (8192 * y bytes)
             5. PlayChoice INST-ROM, if present (0 or 8192 bytes)
             6. PlayChoice PROM, if present (16 bytes Data, 16 bytes CounterOut) (this is often missing, see PC10 ROM-Images for details)
            
             iNES、iNES2.0，是人们发现不同游戏卡带中的硬件控制系统，总结出来一个对应的格式，例如本身8-F保留字在2.0中被利用来存储这些发现的卡带通用信息。
             使用最老版本的iNES格式的游戏中，例如超级马里奥、坦卡大战，ROM格式为：
             1. 16字节头文件
             2. 16kB PRG ROM (1个或2个)
             2. 8kB CHR ROM
             
             16字节头文件中:
             Flags 6
             76543210
             ||||||||
             |||||||+- Mirroring: 0: horizontal (vertical arrangement) (CIRAM A10 = PPU A11)
             |||||||              1: vertical (horizontal arrangement) (CIRAM A10 = PPU A10)
             ||||||+-- 1: Cartridge contains battery-backed PRG RAM ($6000-7FFF) or other persistent memory
             |||||+--- 1: 512-byte trainer at $7000-$71FF (stored before PRG data) // (iNES2.0) 指定是否存在一个trainer，需要顺序加载512字节到$7000-$71FF内存，再加载16kB的PRG ROM
             ||||+---- 1: Ignore mirroring control or above mirroring bit; instead provide four-screen VRAM
             ++++----- Lower nybble of mapper number
             
             */
            
            // 解析头文件 (16字节，前3字节是"NES"字符)
            int rom16kB_count = rom[4];
            int vrom8kB_count = rom[5]; // 如果为0，表明主板将使用板载CHR内存
            
            bit8 flags6 = *(bit8*)&rom[6];
            bit8 flags7 = *(bit8*)&rom[7];
            
            printf("文件长度 %zu\n", length);
            
            printf("[4] 16kB ROM: %d\n\
                   [5] 8kB VROM: %d\n\
                   [6] D0: %d D1: %d D2: %d D3: %d D4: %d D5: %d D6: %d D7: %d\n\
                   [7] 保留0: %d %d %d %d ROM Mapper高4位: %d %d %d %d\n\
                   [8-F] 保留8字节0: %d %d %d %d %d %d %d %d\n\
                   [16]\n",
                   rom[4], rom[5],
                   flags6.get(0), flags6.get(1), flags6.get(2), flags6.get(3), flags6.get(4), flags6.get(5), flags6.get(6), flags6.get(7),
                   flags7.get(0), flags7.get(1), flags7.get(2), flags7.get(3), flags7.get(4), flags7.get(5), flags7.get(6), flags7.get(7),
                   rom[8], rom[9], rom[10], rom[11], rom[12], rom[13], rom[14], rom[15]);
            
            // 只支持 1 or 2 个16kB rom
            RENES_ASSERT(rom16kB_count > 0 && rom16kB_count <= 2);
            
            // 每个bank尺寸是16kB
            const int bankSize = 1024*16;
            const int vromSize = 1024*8;
            
            const uint8_t* romBase = &rom[16];
            const uint8_t* vromBase = romBase + bankSize*rom16kB_count;
            
            // 将PRG ROM载入内存（2个16kB rom）
            for (int i=0; i<2; i++)
            {
                // 获取rom地址
                const uint8_t* romAddrs[2] = {romBase, romBase + bankSize}; {
                    // 如果只有1个16kB的bank，则需要再复制一份（第二份出现在0xC000处，中断向量在每个RPG ROM最后，这里0xFFFA）
                    if (rom16kB_count == 1)
                        romAddrs[1] = romBase;
                }
                
                _mem.loadPRGRom(romAddrs, i);
            }
            
            // 将图案表数据载入VRAM
            for (int i=0; i<vrom8kB_count; i++)
            {
                const uint8_t* vromAddr = vromBase + vromSize*i;
                _ppu.loadPetternTable(vromAddr);
            }
            
            // 设置镜像模式
            _ppu.initMirroring((PPU::MIRRORING_MODE)flags6.get(0));
        }
        
        
        // 执行当前指令
        void run() {
            
            _runningThread = std::thread(_runWrapper, this);
        }
        
        void setDebug(bool debug)
        {
            this->debug = debug;
            
            _cpu.debug = debug;
            setLogEnabled(debug);
            
            log("设置debug模式: %d\n", debug);
        }
        
        bool debug = false;
        float cmd_interval = 0;
        
        
        inline CPU* cpu() { return &_cpu; }
        
        inline PPU* ppu() { return &_ppu; }
        
        inline Memory* mem() { return &_mem; }

        inline Control* ctr() { return &_ctr; }
        
        inline long cpuCycleTime() const { return _cpuCycleTime; }
        
        inline long renderTime() const { return _renderTime; }
        
        // 每帧花费时间: 纳秒
        inline long perFrameTime() const { return _perFrameTime; }
        
        inline bool isRunning() const {return _isRunning;};
        
        // 回调函数
        std::function<bool(CPU*)> cpu_callback;
        std::function<bool(PPU*)> ppu_displayCallback;
        std::function<void()> willRunning;
        
    private:
        
        static
        void _runWrapper(Nes* nes) {
            
            nes->_run();
        }
        
        void _run() {
            
            _stoped = false;
            
            // 初始化cpu
            _cpu.init(&_mem);
            _ppu.init(&_mem);
            
            _isRunning = true;
            if (willRunning)
                willRunning();
            
            //-----------------------------------
            // 控制器处理
            int dstWrite4016 = 0;
            uint16_t dstAddr4016tmp = 0;
            uint16_t dstAddr4016 = 0;
            
            _mem.addWritingObserver(0x4016, [this, &dstWrite4016, &dstAddr4016tmp,  &dstAddr4016](uint16_t addr, uint8_t value){
                
                // 写0x4016 2次，以设置从0x4016读取的硬件信息
                std::function<void(int&,uint16_t&, uint16_t&)> dstAddrWriting = [value](int& dstWrite, uint16_t& dstAddrTmp, uint16_t& dstAddr){
                    
                    int writeBitIndex = (dstWrite+1) % 2;
                    
                    dstAddrTmp &= (0xFF << dstWrite*8); // 清理相反的高/低位
                    dstAddrTmp |= (value << writeBitIndex*8); // 设置对应位
                    
                    dstWrite = (dstWrite+1) % 2;
                    
                    if (dstWrite == 0)
                        dstAddr = dstAddrTmp;
                };
                
                dstAddrWriting(dstWrite4016, dstAddr4016tmp, dstAddr4016);
                
                // 每次重新请求控制器的时候，重置按键
                if (dstWrite4016 == 0 && dstAddr4016 == 0x100)
                {
                    _ctr.reset();
                }
            });
            
            _mem.addReadingObserver(0x4016, [this, &dstWrite4016, &dstAddr4016](uint16_t addr, uint8_t* value, bool* valid){
                
                if (dstAddr4016 == 0x100)
                {
                    *value = _ctr.nextKeyStatue();
                    
                    // dstWrite4016 = 0; ?
                }
            });
            //-----------------------------------
            
            // CPU周期线程 (不用创建线程)
//            std::thread cpu_thread = std::thread([this](){
            {
                
                bool isFirstCPUCycleForFrame = true;
                std::chrono::steady_clock::time_point firstTime;
                
                // 显示器制式数据: NTSC
                const int frame_w = 341;
                const int frame_h = 262;
                const int FPS = 60; // 包含vblank时间
                
                _ppu.setSystemInfo(frame_w, frame_h);
                
                uint32_t cpuCyclesCountForFrame = 0;            // 每一帧内：cpu周期数计数器
                const uint32_t NumScanpointPerCpuCycle = 3;     // 每个cpu周期能绘制的点数(每个像素需要1/3 CPU周期，由CPU和PPU的频率算得，见ppu.hpp)
                const uint32_t TimePerFrame = 1.0 / FPS * 1e9; // 每帧需要的时间(纳秒)
                
                // 主循环
                do {
                    
                    // 第一帧开始进行初始化
                    if (isFirstCPUCycleForFrame)
                    {
                        firstTime = std::chrono::steady_clock::now();
                        isFirstCPUCycleForFrame = false;
                    }
                    
                    // 执行指令
                    int cycles = _cpu.exec();
                    
                    // 发生错误，退出
                    if (_cpu.error)
                        break;
                    
                    // 当前CPU指令周期内，可以绘制多少个点
                    cpuCyclesCountForFrame += cycles;

                    // 满足一次扫描线所经过的CPU周期，执行下面代码，模拟这段时间内，PPU发生的工作
                    bool vblankEvent;
                    _ppu.drawScanline(&vblankEvent, cycles * NumScanpointPerCpuCycle);
                    
                    if (vblankEvent)
                    {
                        // vblank发生的时候，设置NMI中断
                        _cpu.interrupts(CPU::InterruptTypeNMI);
                        
                        // 刷新视图(异步) 刷新率由UI决定
                        ppu_displayCallback(&_ppu);
                    }
                    
                    // 最后一条扫描线完成(第261条扫描线，scanline+1 == 262)
                    if (_ppu.currentFrameOver())
                    {
                        // 模拟等待，模拟每一帧完整时间花费
                        auto currentFrameTime = (std::chrono::steady_clock::now() - firstTime).count(); // 纳秒
                        _perFrameTime = currentFrameTime;
                        _cpuCycleTime  = currentFrameTime / cpuCyclesCountForFrame;
                        
                        // 计数器重置
                        cpuCyclesCountForFrame  = 0;
                        isFirstCPUCycleForFrame = true;
                        
                        // 检查是否需要等待
                        if ( currentFrameTime < TimePerFrame )
                            usleep( (uint32_t)(TimePerFrame - currentFrameTime) / 1000 );
                    }
                    
                }while(cpu_callback(&_cpu) && !_stoped);
                
//            });
            }
            
            // 等待线程结束
//            cpu_thread.join();
            
            if (_cpu.error)
            {
                log("模拟器因故障退出!\n");
            }
            else
            {
                log("模拟器正常退出!\n");
            }
        }
        
        bool _isRunning = false;
        
        // 硬件
        CPU _cpu;
        PPU _ppu;
        Memory _mem;
        Control _ctr;
        
        long _cpuCycleTime;
        long _renderTime;
        long _perFrameTime;
        
        bool _stoped;
        
        std::thread _runningThread;
    };
    
    
}
