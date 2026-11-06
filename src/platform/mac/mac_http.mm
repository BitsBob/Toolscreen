// Synchronous HTTP(S) requests over NSURLSession for the WinHTTP shim (mac_winhttp.cpp).
#import <Foundation/Foundation.h>

#include "mac_http_bridge.h"

#include <cstdlib>
#include <cstring>

extern "C" int MacHttp_Perform(const char* method, const char* url, const char* headersCRLF, const void* body, size_t bodyLength,
                               int timeoutMs, MacHttpResult* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    @autoreleasepool {
        NSURL* nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url ? url : ""]];
        if (!nsurl) {
            out->error = strdup("invalid URL");
            return 0;
        }
        NSMutableURLRequest* request = [NSMutableURLRequest requestWithURL:nsurl];
        request.HTTPMethod = [NSString stringWithUTF8String:method && *method ? method : "GET"];
        request.timeoutInterval = timeoutMs > 0 ? timeoutMs / 1000.0 : 30.0;
        request.cachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        if (headersCRLF && *headersCRLF) {
            for (NSString* line in [[NSString stringWithUTF8String:headersCRLF] componentsSeparatedByString:@"\r\n"]) {
                const NSRange colon = [line rangeOfString:@":"];
                if (colon.location == NSNotFound) continue;
                NSString* name = [[line substringToIndex:colon.location] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
                NSString* value = [[line substringFromIndex:colon.location + 1] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceCharacterSet];
                [request setValue:value forHTTPHeaderField:name];
            }
        }
        if (body && bodyLength) request.HTTPBody = [NSData dataWithBytes:body length:bodyLength];

        __block NSData* responseData = nil;
        __block NSHTTPURLResponse* httpResponse = nil;
        __block NSError* responseError = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        NSURLSessionConfiguration* config = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        NSURLSession* session = [NSURLSession sessionWithConfiguration:config];
        NSURLSessionDataTask* task = [session dataTaskWithRequest:request
                                                completionHandler:^(NSData* data, NSURLResponse* response, NSError* error) {
                                                  responseData = data;
                                                  httpResponse = [response isKindOfClass:NSHTTPURLResponse.class] ? (NSHTTPURLResponse*)response : nil;
                                                  responseError = error;
                                                  dispatch_semaphore_signal(done);
                                                }];
        [task resume];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        [session finishTasksAndInvalidate];

        if (responseError || !httpResponse) {
            out->error = strdup(responseError ? responseError.localizedDescription.UTF8String : "no HTTP response");
            return 0;
        }
        out->status = (int)httpResponse.statusCode;
        NSString* contentType = httpResponse.allHeaderFields[@"Content-Type"];
        if (contentType) out->contentType = strdup(contentType.UTF8String);
        if (responseData.length) {
            out->body = (unsigned char*)malloc(responseData.length);
            memcpy(out->body, responseData.bytes, responseData.length);
            out->bodyLength = responseData.length;
        }
        return 1;
    }
}

extern "C" void MacHttp_FreeResult(MacHttpResult* result) {
    if (!result) return;
    free(result->contentType);
    free(result->body);
    free(result->error);
    memset(result, 0, sizeof(*result));
}
