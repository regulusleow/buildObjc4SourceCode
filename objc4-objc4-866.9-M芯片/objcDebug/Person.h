//
//  Person.h
//  objcDebug
//
//  Created by jiafeng wu on 2023/4/3.
//

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface Person : NSObject

@property (nonatomic, copy) NSString *name;

- (void)saySomething;
- (void)sayWithContent:(NSString *)content;

@end

NS_ASSUME_NONNULL_END
