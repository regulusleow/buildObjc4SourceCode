//
//  main.m
//  objcDebug
//
//  Created by jiafeng wu on 2023/4/3.
//

#import <Foundation/Foundation.h>
#import "Person.h"

int main(int argc, const char * argv[]) {
  @autoreleasepool {
    Person *person = [[Person alloc] init];
    person.name = @"Tom";
    [person sayWithContent:@"hello world"];
  }
  return 0;
}
