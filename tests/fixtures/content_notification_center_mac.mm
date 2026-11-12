// Copyright 2026 Zitter contributors. SPDX-License-Identifier: Apache-2.0
#import <UserNotifications/UserNotifications.h>
#import <objc/runtime.h>

@interface ZitterFixtureNotificationResponse : NSObject
@property(nonatomic, strong) UNNotificationRequest* request;
@property(nonatomic, copy) NSString* actionIdentifier;
@end
@implementation ZitterFixtureNotificationResponse
- (id)notification { return self; }
@end

@interface ZitterFixtureNotificationSettings : NSObject
@end
@implementation ZitterFixtureNotificationSettings
- (UNAuthorizationStatus)authorizationStatus { return UNAuthorizationStatusAuthorized; }
@end

@interface ZitterFixtureNotificationCenter : NSObject
@property(nonatomic, weak) id<UNUserNotificationCenterDelegate> delegate;
@property(nonatomic, strong) NSSet<UNNotificationCategory*>* categories;
@property(nonatomic, strong) NSMutableDictionary<NSString*, UNNotificationRequest*>* requests;
@end

@implementation ZitterFixtureNotificationCenter

- (void)setNotificationCategories:(NSSet<UNNotificationCategory*>*)categories {
  self.categories = categories;
}

- (void)getNotificationSettingsWithCompletionHandler:(void (^)(id))completion {
  completion([[ZitterFixtureNotificationSettings alloc] init]);
}

- (void)addNotificationRequest:(UNNotificationRequest*)request
         withCompletionHandler:(void (^)(NSError*))completion {
  NSString* title = request.content.title;
  NSString* action = nil;
  if ([title isEqualToString:@"Fixture click"])
    action = UNNotificationDefaultActionIdentifier;
  if ([title isEqualToString:@"Fixture dismiss"])
    action = UNNotificationDismissActionIdentifier;
  if ([title isEqualToString:@"Fixture peer button"]) {
    BOOL ownerPresent = NO;
    for (UNNotificationRequest* other in self.requests.allValues) {
      if ([other.content.title isEqualToString:@"Fixture owner updated"] &&
          ![other.identifier isEqualToString:request.identifier]) {
        ownerPresent = YES;
      }
    }
    BOOL buttonPresent = NO;
    for (UNNotificationCategory* category in self.categories) {
      if ([category.identifier isEqualToString:request.content.categoryIdentifier] &&
          [category.actions.lastObject.identifier isEqualToString:@"1"] &&
          [category.actions.lastObject.title isEqualToString:@"Second"]) {
        buttonPresent = YES;
      }
    }
    if (!ownerPresent || !buttonPresent) {
      completion([NSError errorWithDomain:@"ZitterNotificationFixture" code:1 userInfo:nil]);
      return;
    }
    action = @"1";
  }
  self.requests[request.identifier] = request;
  completion(nil);
  if (!action)
    return;
  ZitterFixtureNotificationResponse* response = [[ZitterFixtureNotificationResponse alloc] init];
  response.request = request;
  response.actionIdentifier = action;
  dispatch_after(dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_MSEC * 100),
                 dispatch_get_main_queue(), ^{
    [self.delegate userNotificationCenter:(UNUserNotificationCenter*)self
            didReceiveNotificationResponse:(UNNotificationResponse*)response
                      withCompletionHandler:^{}];
  });
}

- (void)removePendingNotificationRequestsWithIdentifiers:(NSArray<NSString*>*)identifiers {
  [self.requests removeObjectsForKeys:identifiers];
}

- (void)removeDeliveredNotificationsWithIdentifiers:(NSArray<NSString*>*)identifiers {
  [self.requests removeObjectsForKeys:identifiers];
}

@end

extern "C" bool ZitterInstallNotificationFixture() {
  auto* fixture = [[ZitterFixtureNotificationCenter alloc] init];
  fixture.requests = [NSMutableDictionary dictionary];
  Method method = class_getClassMethod([UNUserNotificationCenter class],
                                       @selector(currentNotificationCenter));
  if (!method)
    return false;
  method_setImplementation(method, imp_implementationWithBlock(^id(id receiver) {
    return fixture;
  }));
  return (id)UNUserNotificationCenter.currentNotificationCenter == fixture;
}
