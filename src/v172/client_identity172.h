#pragma once
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <libkern/OSByteOrder.h>

static NSString *regionForBundle172(NSString *bundleID) {
    if ([bundleID isEqual:@"com.gryphline.endfield.ios"]) return @"global";
    if ([bundleID isEqual:@"com.hypergryph.endfield"]) return @"cn";
    return nil;
}

static BOOL digestMatches172(const void *bytes, uint64_t size, NSString *expected) {
    if (!bytes || !size || size>UINT32_MAX || expected.length!=64) return NO;
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256(bytes,(CC_LONG)size,digest);
    char hex[65]; for (size_t i=0;i<sizeof digest;i++) snprintf(hex+2*i,3,"%02x",digest[i]);
    return [expected isEqual:@(hex)];
}

// PlayCover may change the Mach-O platform and signature. Identify the arm64
// code, not the outer signature, without accepting a UUID alone as code proof.
static BOOL unityMatches172(NSString *path, NSDictionary *profile) {
    NSData *data=[NSData dataWithContentsOfFile:path options:NSDataReadingMappedIfSafe error:NULL];
    if(data.length<sizeof(struct mach_header_64))return NO;
    const uint8_t *bytes=data.bytes;uint64_t offset=0,size=data.length;
    uint32_t magic;memcpy(&magic,bytes,sizeof magic);
    if(magic==FAT_CIGAM || magic==FAT_CIGAM_64){
        const struct fat_header *fat=(const void *)bytes;
        uint32_t count=OSSwapBigToHostInt32(fat->nfat_arch);
        size_t entrySize=magic==FAT_CIGAM_64 ? sizeof(struct fat_arch_64) : sizeof(struct fat_arch);
        if(count>64 || sizeof(*fat)+(uint64_t)count*entrySize>data.length)return NO;
        BOOL found=NO;
        for(uint32_t i=0;i<count;i++){
            const struct fat_arch *arch=(const void *)(bytes+sizeof(*fat)+i*entrySize);
            if(OSSwapBigToHostInt32(arch->cputype)!=CPU_TYPE_ARM64)continue;
            if(magic==FAT_CIGAM_64){const struct fat_arch_64 *a=(const void *)arch;offset=OSSwapBigToHostInt64(a->offset);size=OSSwapBigToHostInt64(a->size);}
            else{offset=OSSwapBigToHostInt32(arch->offset);size=OSSwapBigToHostInt32(arch->size);}
            found=YES;break;
        }
        if(!found)return NO;
    }
    if(offset>data.length || size>data.length-offset || size<sizeof(struct mach_header_64))return NO;
    bytes+=offset;
    const struct mach_header_64 *header=(const void *)bytes;
    if(header->magic!=MH_MAGIC_64 || header->cputype!=CPU_TYPE_ARM64 || header->sizeofcmds>size-sizeof(*header))return NO;
    size_t cursor=sizeof(*header),end=cursor+header->sizeofcmds;
    BOOL uuid=NO,text=NO;
    for(uint32_t i=0;i<header->ncmds;i++){
        if(cursor>end || end-cursor<sizeof(struct load_command))return NO;
        const struct load_command *cmd=(const void *)(bytes+cursor);
        if(cmd->cmdsize<sizeof(*cmd) || cmd->cmdsize>end-cursor)return NO;
        if(cmd->cmd==LC_UUID){
            if(uuid || cmd->cmdsize!=sizeof(struct uuid_command))return NO;
            const struct uuid_command *u=(const void *)cmd;
            uuid=[[[NSUUID alloc] initWithUUIDBytes:u->uuid].UUIDString.lowercaseString isEqual:profile[@"unityUUID"]];
            if(!uuid)return NO;
        }else if(cmd->cmd==LC_SEGMENT_64){
            const struct segment_command_64 *seg=(const void *)cmd;
            if(cmd->cmdsize<sizeof(*seg) || seg->nsects>(cmd->cmdsize-sizeof(*seg))/sizeof(struct section_64))return NO;
            if(!strncmp(seg->segname,"__TEXT",16) && (seg->initprot&VM_PROT_EXECUTE)){
                const struct section_64 *s=(const void *)(seg+1);
                for(uint32_t j=0;j<seg->nsects;j++,s++)if(!strncmp(s->sectname,"__text",16) && !strncmp(s->segname,"__TEXT",16)){
                    // Hash the file range actually mapped at this section's VM address.
                    if(text || s->offset<end || seg->fileoff>size || seg->filesize>size-seg->fileoff ||
                       s->offset<seg->fileoff || s->addr<seg->vmaddr)return NO;
                    uint64_t delta=s->addr-seg->vmaddr;
                    if(delta!=s->offset-seg->fileoff || delta>seg->filesize || s->size>seg->filesize-delta ||
                       delta>seg->vmsize || s->size>seg->vmsize-delta)return NO;
                    text=digestMatches172(bytes+s->offset,s->size,profile[@"textSHA256"]);
                    if(!text)return NO;
                }
            }
        }
        cursor+=cmd->cmdsize;
    }
    return cursor==end && uuid && text;
}

static BOOL clientMatches172(NSString *path, NSDictionary *info, NSDictionary *profile) {
    if (![profile[@"version"] isEqual:@"1.7.2"] || ![info[@"CFBundleShortVersionString"] isEqual:profile[@"version"]] ||
        ![info[@"CFBundleVersion"] isEqual:profile[@"build"]] || ![info[@"CFBundleIdentifier"] isEqual:profile[@"bundleID"]] ||
        ![info[@"CFBundleExecutable"] isEqual:@"Endfield"]) return NO;
    NSData *metadata=[NSData dataWithContentsOfFile:[path stringByAppendingPathComponent:@"Data/Managed/Metadata/global-metadata.dat"]
                                            options:NSDataReadingMappedIfSafe error:NULL];
    return digestMatches172(metadata.bytes,metadata.length,profile[@"metadataSHA256"]) &&
        unityMatches172([path stringByAppendingPathComponent:@"Frameworks/UnityFramework.framework/UnityFramework"],profile);
}
