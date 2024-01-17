//
//  ViewController.h
//  iOS-demo
//
//  Created by DZ0401034 on 2024/1/12.
//

#import <UIKit/UIKit.h>
#include <lite-obs/lite_obs.h>
#include "TcpServer.hpp"

@interface ViewController : UIViewController {
    lite_obs_api *api;
    TcpServer *server;
    lite_obs_media_source_api *source;
}

- (void)addLog:(NSString *)text;

@end

