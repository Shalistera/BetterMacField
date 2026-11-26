"""Verified iOS builds. The launcher and graphics bundle get the same table."""


def client(bundle, uuid, text_sha, callbacks, wrapper, getter, flag):
    return dict(bundleID=bundle, version='1.7.2', build='0', unityUUID=uuid,
                metadataSHA256='3de06a6ee4f48cd920f2dd617f6a7ebb4755ba5308d9ad592b328a06c52def44',
                textSHA256=text_sha, renderCallbacks=callbacks, cppWrapper=wrapper,
                cppGetter=getter, cppFlag=flag)


CLIENTS = {
    'global': client('com.gryphline.endfield.ios', '0f92770a-6938-397f-a0f2-272e994ba286',
                     '790ab410efda03b5d0d8e6c39cad4bbb7b45f9f8274b33144d3f94708a26aeb1',
                     [0x10a35f58, 0x10a36178, 0x10a36634], 0x0fb1f610, 0x1054c020, 0x1562b369),
    'cn': client('com.hypergryph.endfield', '5e01e569-4b4d-35aa-b5d1-8f3f97c5278a',
                 '5d8e7fccb12140054822d698d42bcf8460912acde2d47c7bd19298a5d64fe76f',
                 [0x10a7a620, 0x10a7a840, 0x10a7acfc], 0x0fb63410, 0x105906e8, 0x159ffb69),
}
