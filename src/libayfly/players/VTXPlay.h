struct VTX_File
{
    unsigned char Id0, Id1;
    unsigned char Mode;
    unsigned char Loop0, Loop1;
    unsigned char ChipFrq0, ChipFrq1, ChipFrq2, ChipFrq3;
    unsigned char InterFrq;
    unsigned char Year0, Year1;
    unsigned char UnpackSize0, UnpackSize1, UnpackSize2, UnpackSize3;
};

#define VTX_Id (header->Id0 | (header->Id1 << 8))
#define VTX_Loop (header->Loop0 | (header->Loop1 << 8))
#define VTX_ChipFrq (header->ChipFrq0 | (header->ChipFrq1 << 8) | (header->ChipFrq2 << 16) | (header->ChipFrq3 << 24))
#define VTX_Year (header->Year0 | (header->Year1 << 8))
#define VTX_UnpackSize (header->UnpackSize0 | (header->UnpackSize1 << 8) | (header->UnpackSize2 << 16) | (header->UnpackSize3 << 24))

struct VTX_SongInfo
{
    int i;
    unsigned long VTX_Offset;
    unsigned long Position_In_VTX;

};

#define VTX ((VTX_SongInfo *)info.data)

// Register dumps are 14 bytes/tick; 16 MB is ~6.5 h at 50 Hz.
#define VTX_MAX_UNPACK_SIZE (16UL * 1024 * 1024)

// Corrupt VTX headers carry no NUL before EOF, so bound every string walk.
static unsigned long vtx_strnlen(const unsigned char *p, const AYSongInfo &info)
{
    if(p < info.file_data || (unsigned long)(p - info.file_data) >= info.file_len)
        return 0;
    const unsigned long avail = info.file_len - (unsigned long)(p - info.file_data);
    const void *nul = memchr(p, 0, avail);
    return nul ? (unsigned long)((const unsigned char *)nul - p) : avail;
}

// Poison the header copy so GetInfo yields a zero-length, inert song.
static void vtx_invalidate(AYSongInfo &info)
{
    VTX_File *header = (VTX_File *)info.file_data;
    header->UnpackSize0 = header->UnpackSize1 = header->UnpackSize2 = header->UnpackSize3 = 0;
    header->Loop0 = header->Loop1 = 0;
}

void VTX_Init(AYSongInfo &info)
{
    unsigned char *module = info.file_data;
    VTX_File *header = (VTX_File *)module;
    if(info.data)
    {
        delete (VTX_SongInfo *)info.data;
        info.data = 0;
    }
    info.data = (void *)new VTX_SongInfo;
    if(!info.data)
        return;
    memset(info.data, 0, sizeof(VTX_SongInfo));
    if((VTX_Id != 0x5941) && (VTX_Id != 0x4d59) && (VTX_Id != 0x7961) && (VTX_Id != 0x6d79))
    {
        return;
    }
    if((VTX_Id == 0x7961) || (VTX_Id == 0x5941))
        info.chip_type = 0;
    else
        info.chip_type = 1;

    // A corrupt UnpackSize would allocate gigabytes or desync VTX_Play's
    // stride math; reject instead of trusting the header.
    if(info.file_len < sizeof(VTX_File) || VTX_UnpackSize == 0 ||
        VTX_UnpackSize > VTX_MAX_UNPACK_SIZE || (VTX_UnpackSize % 14) != 0)
    {
        vtx_invalidate(info);
        return;
    }

    ay_setchiptype(&info, info.chip_type);
    ay_setayfreq(&info, VTX_ChipFrq);
    if(header->InterFrq > 0)
        ay_setintfreq(&info, header->InterFrq); // 0 skips SetParameters in ay.cpp

    unsigned char *p = info.file_data + sizeof(VTX_File);
    unsigned long slen = vtx_strnlen(p, info);
    p += slen + 1;
    slen = vtx_strnlen(p, info);
    p += slen + 1;
    if((VTX_Id == 0x7961) || (VTX_Id == 0x6d79))
    {
        slen = vtx_strnlen(p, info);
        p += slen + 1;
        slen = vtx_strnlen(p, info);
        p += slen + 1;
        slen = vtx_strnlen(p, info);
        p += slen + 1;
    }
    // file_len - offset underflows in ay_sys_decodelha past this point.
    if((unsigned long)(p - info.file_data) >= info.file_len)
    {
        vtx_invalidate(info);
        return;
    }

    if(info.module != 0)
    {
        delete[] info.module;
        info.module_len = VTX_UnpackSize * 2;
        info.module = new unsigned char[info.module_len];
        memset(info.module, 0, info.module_len);
    }
    ay_sys_decodelha(info, p - info.file_data);
}

void VTX_Play(AYSongInfo &info)
{
    unsigned char *module = info.module;
    unsigned long k = VTX->VTX_Offset;
    for(int i = 0; i <= 12; i++)
    {
        switch(i)
        {
            case 1:
            case 3:
            case 5:
                ay_writeay(&info, i, module[VTX->Position_In_VTX + k] & 15);
                break;
            case 6:
                ay_writeay(&info, AY_NOISE_PERIOD, module[VTX->Position_In_VTX + k] & 31);
                break;
            case 7:
                ay_writeay(&info, AY_MIXER, module[VTX->Position_In_VTX + k] & 63);
                break;
            case 8:
                ay_writeay(&info, AY_CHNL_A_VOL, module[VTX->Position_In_VTX + k] & 31);
                break;
            case 9:
                ay_writeay(&info, AY_CHNL_B_VOL, module[VTX->Position_In_VTX + k] & 31);
                break;
            case 10:
                ay_writeay(&info, AY_CHNL_C_VOL, module[VTX->Position_In_VTX + k] & 31);
                break;
            default:
                ay_writeay(&info, i, module[VTX->Position_In_VTX + k]);
                break;
        }
        k += info.Length;
    }
    if(module[VTX->Position_In_VTX + k] != 255)
        ay_writeay(&info, AY_ENV_SHAPE, module[VTX->Position_In_VTX + k] & 15);
    VTX->Position_In_VTX++;
    if(VTX->Position_In_VTX > info.Length)
        VTX->Position_In_VTX = info.Loop;
}

void VTX_Cleanup(AYSongInfo &info)
{
    if(info.data)
    {
        delete (VTX_SongInfo *)info.data;
        info.data = 0;
    }
}

void VTX_GetInfo(AYSongInfo &info)
{
    unsigned char *module = info.file_data;
    VTX_File *header = (VTX_File *)module;
    info.Length = VTX_UnpackSize / 14;
    info.Loop = VTX_Loop;
    unsigned char *p = info.file_data + sizeof(VTX_File);
    int len = vtx_strnlen(p, info);
    info.Name = ay_sys_getstr(p, len);
    p += len + 1;
    len = vtx_strnlen(p, info);
    info.Author = ay_sys_getstr(p, len);
    p += len + 1;
    if((VTX_Id == 0x7961) || (VTX_Id == 0x6d79))
    {
        len = vtx_strnlen(p, info);
        info.PrgName = ay_sys_getstr(p, len);
        p += len + 1;
        len = vtx_strnlen(p, info);
        info.TrackName = ay_sys_getstr(p, len);
        p += len + 1;
        len = vtx_strnlen(p, info);
        info.CompName = ay_sys_getstr(p, len);
        p += len + 1;
    }
}

bool VTX_Detect(unsigned char *module, unsigned long length)
{
    if((((module[0] == 'a') && (module[1] == 'y')) || ((module[0] == 'y') && (module[1] == 'm')) || ((module[0] == 'A') && (module[1] == 'Y')) || ((module[0] == 'Y') && (module[1] == 'M'))) && (module[2] <= 6))
        return true;
    return false;

}
