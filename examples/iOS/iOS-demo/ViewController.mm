//
//  ViewController.m
//  iOS-demo
//
//  Created by DZ0401034 on 2024/1/12.
//

#import "ViewController.h"
#include <pthread.h>

@interface ViewController () <UITextViewDelegate>

@property (nonatomic, strong) UITextView *textView;

@end


@implementation ViewController

- (void)addLog:(NSString *)text {
    dispatch_async(dispatch_get_main_queue(), ^{
        [self.textView insertText: text];
    });
}

- (void)viewDidLoad {
    [super viewDidLoad];
    // Do any additional setup after loading the view.
    
    api = lite_obs_api_new();
    api->lite_obs_reset_video(api, 1280, 720, 20);
    api->lite_obs_reset_audio(api, 48000);
    
    source = lite_obs_media_source_new(api, source_type::SOURCE_VIDEO);
    
    UIImage *image = [UIImage imageNamed:@"live_gift_level6"];
    int imageWidth = image.size.width;
    int imageHeight = image.size.height;
    CGImageRef cgImage = [image CGImage];
    unsigned char* srcPixel = (unsigned char*)calloc(1, (int)imageWidth*(int)imageHeight*4);
    CGColorSpaceRef genericRGBColorspace = CGColorSpaceCreateDeviceRGB();
    CGContextRef imageContext = CGBitmapContextCreate(srcPixel, imageWidth, imageHeight, 8, 4*imageWidth, genericRGBColorspace, kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGContextDrawImage(imageContext, CGRectMake(0.0, 0.0, imageWidth, imageHeight), cgImage);
    CGContextRelease(imageContext);
    CGColorSpaceRelease(genericRGBColorspace);
    
    source->output_video3(source, srcPixel, imageWidth, imageHeight);
    free(srcPixel);
    
    self.view.backgroundColor = UIColor.whiteColor;
    {
        self.textView = [[UITextView alloc] initWithFrame:self.view.bounds];
        self.textView.delegate = self;
        self.textView.font = [UIFont systemFontOfSize:16];
        self.textView.layer.borderWidth = 1.0;
        self.textView.layer.borderColor = [UIColor lightGrayColor].CGColor;
        self.textView.editable = YES;
        [self.view addSubview:self.textView];
    }
    {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeCustom];
        btn.frame = CGRectMake(0, 200, 80, 40);
        [btn setTitle:@"start stream" forState:0];
        btn.backgroundColor = UIColor.brownColor;
        [btn addTarget:self action:@selector(onStart) forControlEvents:UIControlEventTouchUpInside];
        [self.view addSubview:btn];
    }
    
    {
        UIButton *btn = [UIButton buttonWithType:UIButtonTypeCustom];
        btn.frame = CGRectMake(0, 300, 80, 40);
        [btn setTitle:@"stop stream" forState:0];
        btn.backgroundColor = UIColor.brownColor;
        [btn addTarget:self action:@selector(onStop) forControlEvents:UIControlEventTouchUpInside];
        [self.view addSubview:btn];
    }
    
    SocketCallback cb;
    cb.userdata = (__bridge void *)(self);
    cb.connected = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"new client connected\n"];
    };
    cb.disconnected = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"client disconnected\n"];
    };
    cb.log = [](const char *text, void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: [[NSString alloc] initWithUTF8String:text]];
    };
    server = new TcpServer(cb);
    server->start(2345);
}

- (void)viewDidUnload {
    lite_obs_api_delete(&api);
    server->stop();
    delete server;
}

- (void)onStop {
    api->lite_obs_stop_output(api);
}

- (void)onStart {
    
    lite_obs_output_callbak cb{};
    cb.start = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===start\n"];
    };
    cb.stop = [](int code, const char *msg, void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===stop\n"];
    };
    cb.starting = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===starting\n"];
    };
    cb.stopping = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===stopping\n"];
    };
    cb.activate = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===activate\n"];
    };
    cb.deactivate = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===deactivate\n"];
    };
    cb.reconnect = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===reconnect\n"];
    };
    cb.reconnect_success = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===reconnect_success\n"];
    };
    cb.connected = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===connected\n"];
    };
    cb.first_media_packet = [](void *param){
        ViewController *view = (__bridge ViewController *)(param);
        [view addLog: @"===first_media_packet\n"];
    };
    cb.opaque = (__bridge void *)(self);
    int id = server->clientSocket();
    api->lite_obs_start_output(api, output_type::iOS_usb, (void *)&id, 4000, 160, cb);
    
}


@end
