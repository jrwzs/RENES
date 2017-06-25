//
//  ViewController.m
//  RENES
//
//  Created by rexq on 2017/6/25.
//  Copyright © 2017年 com.rexq. All rights reserved.
//

#import "ViewController.h"
#include "src/renes.hpp"
#include <string>

@interface ViewController()
{
    ReNes::Nes _nes;
    
    dispatch_semaphore_t _nextSem;
}

@property (nonatomic) IBOutlet NSTextView* memView;
@property (nonatomic) IBOutlet NSTextField* registersView;

@end

@implementation ViewController

- (void)viewDidLoad {
    [super viewDidLoad];

    // Do any additional setup after loading the view.
    
    _nextSem = dispatch_semaphore_create(0);
    
    // 异步
    dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
        
        NSString* filePath = [[NSBundle mainBundle] pathForResource:@"OUR.NES" ofType:@""];
        NSData* data = [NSData dataWithContentsOfFile:filePath];
        printf("文件长度 %d\n", data.length);
        
        _nes.callback = [self](){
            [self updateView];
            
            dispatch_semaphore_wait(_nextSem, DISPATCH_TIME_FOREVER);
            
            return true;
        };
        _nes.loadRom((const uint8_t*)[data bytes], [data length]);
        
        _nes.run();
        
        
        
    });
}

- (void) dumpMemToView
{
    dispatch_async(dispatch_get_main_queue(), ^{
        
        std::string str;
        int count = 0x10000;
        
        bool printLineNum = true;
        
        char buffer[10];
        
        for (int i=0; i<count; i++)
        {
            if (i > 1 && i % 0x10 == 0)
            {
                str += "\r";
                printLineNum = true;
            }
            
            if (printLineNum)
            {
                sprintf(buffer, "0x%04X  ", i);
                str += buffer;
                printLineNum = false;
            }
            
            uint8_t data = _nes.mem()->read8bitData(i);
            
            sprintf(buffer, "%02X", data);
            
            str += buffer;
            
            
            if (i < count-1)
            {
                {
                    str += " ";
                }
                
                
            }
        }
        _memView.string = [NSString stringWithUTF8String:str.c_str()];
    });
}

- (void) updateView
{
    [self updateRegisters];
    
    // 打印内存
    [self dumpMemToView];
}

- (void) updateRegisters
{
    dispatch_async(dispatch_get_main_queue(), ^{
        
        const ReNes::CPU::__registers& regs = _nes.cpu()->regs;
        
        _registersView.stringValue = [NSString stringWithFormat:@"PC: 0x%04X SP: 0x%04X\n\
C:%d Z:%d I:%d D:%d B:%d _:%d V:%d N:%d\n\
A:%d X:%d Y:%d", regs.PC, regs.SP,
                                      regs.P.get(ReNes::CPU::__registers::C),
                                      regs.P.get(ReNes::CPU::__registers::Z),
                                      regs.P.get(ReNes::CPU::__registers::I),
                                      regs.P.get(ReNes::CPU::__registers::D),
                                      regs.P.get(ReNes::CPU::__registers::B),
                                      regs.P.get(ReNes::CPU::__registers::_),
                                      regs.P.get(ReNes::CPU::__registers::V),
                                      regs.P.get(ReNes::CPU::__registers::N),
                                      regs.A,regs.X,regs.Y];
        
        
    });
}

- (IBAction) next:(id)sender
{
    dispatch_semaphore_signal(_nextSem);
}

- (void)setRepresentedObject:(id)representedObject {
    [super setRepresentedObject:representedObject];

    // Update the view, if already loaded.
}


@end