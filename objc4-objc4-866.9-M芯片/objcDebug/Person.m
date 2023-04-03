//
//  Person.m
//  objcDebug
//
//  Created by jiafeng wu on 2023/4/3.
//

#import "Person.h"

@interface Person()

@end

@implementation Person

- (void)saySomething {
  NSLog(@"%@ say: hello world", self.name);
}

- (void)sayWithContent:(NSString *)content {
  NSLog(@"%@ say: %@", self.name, content);
}

@end
