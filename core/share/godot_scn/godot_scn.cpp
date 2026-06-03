#include <slang-cpp-prelude.h>

#ifdef SLANG_PRELUDE_NAMESPACE
using namespace SLANG_PRELUDE_NAMESPACE;
#endif


#line 1 "openusd-fabric/lean/shaders/godot_scn.slang"
struct GodotResourceHeader_0
{
    uint32_t big_endian_0;
    uint32_t use_64bit_0;
    uint32_t ver_major_0;
    uint32_t ver_minor_0;
    uint32_t format_version_0;
    uint32_t flags_0;
    uint32_t uid_lo_0;
    uint32_t uid_hi_0;
};


#line 267
struct GlobalParams_0
{
    RWStructuredBuffer<uint32_t> buf_0;
    StructuredBuffer<uint32_t> str_data_0;
    StructuredBuffer<uint32_t> int_data_0;
    StructuredBuffer<float> float_data_0;
};


#line 267
struct KernelContext_0
{
    GlobalParams_0* globalParams_0;
};


#line 198
static void godot_write_header_0(RWStructuredBuffer<uint32_t> buf_1, uint32_t * offset_0, GodotResourceHeader_0 * hdr_0)
{

#line 199
    *(&(buf_1)[*offset_0 / 4U]) = 1129468754U;
    uint32_t _S1 = *offset_0 + 4U;

#line 200
    *offset_0 = _S1;
    *(&(buf_1)[_S1 / 4U]) = hdr_0->big_endian_0;
    uint32_t _S2 = *offset_0 + 4U;

#line 202
    *offset_0 = _S2;
    *(&(buf_1)[_S2 / 4U]) = hdr_0->use_64bit_0;
    uint32_t _S3 = *offset_0 + 4U;

#line 204
    *offset_0 = _S3;
    *(&(buf_1)[_S3 / 4U]) = hdr_0->ver_major_0;
    uint32_t _S4 = *offset_0 + 4U;

#line 206
    *offset_0 = _S4;
    *(&(buf_1)[_S4 / 4U]) = hdr_0->ver_minor_0;
    uint32_t _S5 = *offset_0 + 4U;

#line 208
    *offset_0 = _S5;
    *(&(buf_1)[_S5 / 4U]) = hdr_0->format_version_0;
    *offset_0 = *offset_0 + 4U;
    return;
}


#line 259
static void godot_write_reserved_fields_0(RWStructuredBuffer<uint32_t> buf_2, uint32_t * offset_1)
{

#line 259
    uint32_t i_0 = 0U;
    for(;;)
    {

#line 260
        if(i_0 < 11U)
        {
        }
        else
        {

#line 260
            break;
        }

#line 261
        *(&(buf_2)[*offset_1 / 4U + i_0]) = 0U;

#line 260
        i_0 = i_0 + 1U;

#line 260
    }


    *offset_1 = *offset_1 + 44U;
    return;
}


#line 234
static void godot_write_string_table_header_0(RWStructuredBuffer<uint32_t> buf_3, uint32_t * offset_2, uint32_t count_0)
{

#line 235
    *(&(buf_3)[*offset_2 / 4U]) = count_0;
    *offset_2 = *offset_2 + 4U;
    return;
}


#line 239
static void godot_write_ext_resource_count_0(RWStructuredBuffer<uint32_t> buf_4, uint32_t * offset_3, uint32_t count_1)
{

#line 240
    *(&(buf_4)[*offset_3 / 4U]) = count_1;
    *offset_3 = *offset_3 + 4U;
    return;
}


#line 244
static void godot_write_int_resource_count_0(RWStructuredBuffer<uint32_t> buf_5, uint32_t * offset_4, uint32_t count_2)
{

#line 245
    *(&(buf_5)[*offset_4 / 4U]) = count_2;
    *offset_4 = *offset_4 + 4U;
    return;
}


#line 33
static void godot_store_u32_0(RWStructuredBuffer<uint32_t> buf_6, uint32_t * offset_5, uint32_t value_0)
{

#line 34
    *(&(buf_6)[*offset_5 / 4U]) = value_0;
    *offset_5 = *offset_5 + 4U;
    return;
}


#line 38
static void godot_store_u64_0(RWStructuredBuffer<uint32_t> buf_7, uint32_t * offset_6, uint32_t value_lo_0, uint32_t value_hi_0)
{

#line 39
    *(&(buf_7)[*offset_6 / 4U]) = value_lo_0;
    uint32_t _S6 = *offset_6 + 4U;

#line 40
    *offset_6 = _S6;
    *(&(buf_7)[_S6 / 4U]) = value_hi_0;
    *offset_6 = *offset_6 + 4U;
    return;
}


#line 45
static void godot_store_float_0(RWStructuredBuffer<uint32_t> buf_8, uint32_t * offset_7, float value_1)
{

#line 46
    *(&(buf_8)[*offset_7 / 4U]) = (F32_asuint((value_1)));
    *offset_7 = *offset_7 + 4U;
    return;
}


#line 50
static void godot_write_variant_tag_0(RWStructuredBuffer<uint32_t> buf_9, uint32_t * offset_8, uint32_t tag_0)
{

#line 51
    *(&(buf_9)[*offset_8 / 4U]) = tag_0;
    *offset_8 = *offset_8 + 4U;
    return;
}


#line 55
static void godot_write_nil_0(RWStructuredBuffer<uint32_t> buf_10, uint32_t * offset_9)
{

#line 56
    *(&(buf_10)[*offset_9 / 4U]) = 1U;
    *offset_9 = *offset_9 + 4U;
    return;
}


#line 60
static void godot_write_bool_0(RWStructuredBuffer<uint32_t> buf_11, uint32_t * offset_10, uint32_t value_2)
{

#line 61
    *(&(buf_11)[*offset_10 / 4U]) = 2U;
    uint32_t _S7 = *offset_10 + 4U;

#line 62
    *offset_10 = _S7;
    *(&(buf_11)[_S7 / 4U]) = value_2;
    *offset_10 = *offset_10 + 4U;
    return;
}


#line 67
static void godot_write_int_0(RWStructuredBuffer<uint32_t> buf_12, uint32_t * offset_11, int32_t value_3)
{

#line 68
    *(&(buf_12)[*offset_11 / 4U]) = 3U;
    uint32_t _S8 = *offset_11 + 4U;

#line 69
    *offset_11 = _S8;
    *(&(buf_12)[_S8 / 4U]) = (I32_asuint((value_3)));
    *offset_11 = *offset_11 + 4U;
    return;
}


#line 74
static void godot_write_vector3_0(RWStructuredBuffer<uint32_t> buf_13, uint32_t * offset_12, Vector<float, 3>  v_0)
{

#line 75
    *(&(buf_13)[*offset_12 / 4U]) = 12U;
    uint32_t _S9 = *offset_12 + 4U;

#line 76
    *offset_12 = _S9;
    *(&(buf_13)[_S9 / 4U]) = (F32_asuint((v_0.x)));
    uint32_t _S10 = *offset_12 + 4U;

#line 78
    *offset_12 = _S10;
    *(&(buf_13)[_S10 / 4U]) = (F32_asuint((v_0.y)));
    uint32_t _S11 = *offset_12 + 4U;

#line 80
    *offset_12 = _S11;
    *(&(buf_13)[_S11 / 4U]) = (F32_asuint((v_0.z)));
    *offset_12 = *offset_12 + 4U;
    return;
}


#line 85
static void godot_write_quaternion_0(RWStructuredBuffer<uint32_t> buf_14, uint32_t * offset_13, Vector<float, 4>  q_0)
{

#line 86
    *(&(buf_14)[*offset_13 / 4U]) = 14U;
    uint32_t _S12 = *offset_13 + 4U;

#line 87
    *offset_13 = _S12;
    *(&(buf_14)[_S12 / 4U]) = (F32_asuint((q_0.x)));
    uint32_t _S13 = *offset_13 + 4U;

#line 89
    *offset_13 = _S13;
    *(&(buf_14)[_S13 / 4U]) = (F32_asuint((q_0.y)));
    uint32_t _S14 = *offset_13 + 4U;

#line 91
    *offset_13 = _S14;
    *(&(buf_14)[_S14 / 4U]) = (F32_asuint((q_0.z)));
    uint32_t _S15 = *offset_13 + 4U;

#line 93
    *offset_13 = _S15;
    *(&(buf_14)[_S15 / 4U]) = (F32_asuint((q_0.w)));
    *offset_13 = *offset_13 + 4U;
    return;
}


#line 98
static void godot_write_color_0(RWStructuredBuffer<uint32_t> buf_15, uint32_t * offset_14, Vector<float, 4>  c_0)
{

#line 99
    *(&(buf_15)[*offset_14 / 4U]) = 20U;
    uint32_t _S16 = *offset_14 + 4U;

#line 100
    *offset_14 = _S16;
    *(&(buf_15)[_S16 / 4U]) = (F32_asuint((c_0.x)));
    uint32_t _S17 = *offset_14 + 4U;

#line 102
    *offset_14 = _S17;
    *(&(buf_15)[_S17 / 4U]) = (F32_asuint((c_0.y)));
    uint32_t _S18 = *offset_14 + 4U;

#line 104
    *offset_14 = _S18;
    *(&(buf_15)[_S18 / 4U]) = (F32_asuint((c_0.z)));
    uint32_t _S19 = *offset_14 + 4U;

#line 106
    *offset_14 = _S19;
    *(&(buf_15)[_S19 / 4U]) = (F32_asuint((c_0.w)));
    *offset_14 = *offset_14 + 4U;
    return;
}


#line 111
static void godot_write_transform3d_0(RWStructuredBuffer<uint32_t> buf_16, uint32_t * offset_15, Vector<float, 3>  basis_0, Vector<float, 3>  origin_0)
{

#line 112
    *(&(buf_16)[*offset_15 / 4U]) = 17U;
    uint32_t _S20 = *offset_15 + 4U;

#line 113
    *offset_15 = _S20;
    *(&(buf_16)[_S20 / 4U]) = (F32_asuint((origin_0.x)));
    uint32_t _S21 = *offset_15 + 4U;

#line 115
    *offset_15 = _S21;
    *(&(buf_16)[_S21 / 4U]) = (F32_asuint((origin_0.y)));
    uint32_t _S22 = *offset_15 + 4U;

#line 117
    *offset_15 = _S22;
    *(&(buf_16)[_S22 / 4U]) = (F32_asuint((origin_0.z)));
    *offset_15 = *offset_15 + 4U;
    return;
}


#line 132
static void godot_write_dict_header_0(RWStructuredBuffer<uint32_t> buf_17, uint32_t * offset_16, uint32_t count_3)
{

#line 133
    *(&(buf_17)[*offset_16 / 4U]) = 26U;
    uint32_t _S23 = *offset_16 + 4U;

#line 134
    *offset_16 = _S23;
    *(&(buf_17)[_S23 / 4U]) = count_3;
    *offset_16 = *offset_16 + 4U;
    return;
}


#line 139
static void godot_write_array_header_0(RWStructuredBuffer<uint32_t> buf_18, uint32_t * offset_17, uint32_t count_4)
{

#line 140
    *(&(buf_18)[*offset_17 / 4U]) = 30U;
    uint32_t _S24 = *offset_17 + 4U;

#line 141
    *offset_17 = _S24;
    *(&(buf_18)[_S24 / 4U]) = count_4;
    *offset_17 = *offset_17 + 4U;
    return;
}


#line 168
static void godot_write_packed_string_array_header_0(RWStructuredBuffer<uint32_t> buf_19, uint32_t * offset_18, uint32_t count_5)
{

#line 169
    *(&(buf_19)[*offset_18 / 4U]) = 34U;
    uint32_t _S25 = *offset_18 + 4U;

#line 170
    *offset_18 = _S25;
    *(&(buf_19)[_S25 / 4U]) = count_5;
    *offset_18 = *offset_18 + 4U;
    return;
}


#line 146
static void godot_write_packed_int32_array_0(RWStructuredBuffer<uint32_t> buf_20, uint32_t * offset_19, StructuredBuffer<uint32_t> data_0, uint32_t count_6)
{

#line 147
    *(&(buf_20)[*offset_19 / 4U]) = 32U;
    uint32_t _S26 = *offset_19 + 4U;

#line 148
    *offset_19 = _S26;
    *(&(buf_20)[_S26 / 4U]) = count_6;
    *offset_19 = *offset_19 + 4U;

#line 150
    uint32_t i_1 = 0U;
    for(;;)
    {

#line 151
        if(i_1 < count_6)
        {
        }
        else
        {

#line 151
            break;
        }

#line 152
        *(&(buf_20)[*offset_19 / 4U + i_1]) = data_0.Load(i_1);

#line 151
        i_1 = i_1 + 1U;

#line 151
    }


    *offset_19 = *offset_19 + count_6 * 4U;
    return;
}


#line 157
static void godot_write_packed_float32_array_0(RWStructuredBuffer<uint32_t> buf_21, uint32_t * offset_20, StructuredBuffer<float> data_1, uint32_t count_7)
{

#line 158
    *(&(buf_21)[*offset_20 / 4U]) = 33U;
    uint32_t _S27 = *offset_20 + 4U;

#line 159
    *offset_20 = _S27;
    *(&(buf_21)[_S27 / 4U]) = count_7;
    *offset_20 = *offset_20 + 4U;

#line 161
    uint32_t i_2 = 0U;
    for(;;)
    {

#line 162
        if(i_2 < count_7)
        {
        }
        else
        {

#line 162
            break;
        }

#line 163
        *(&(buf_21)[*offset_20 / 4U + i_2]) = (F32_asuint((data_1.Load(i_2))));

#line 162
        i_2 = i_2 + 1U;

#line 162
    }


    *offset_20 = *offset_20 + count_7 * 4U;
    return;
}


#line 122
static void godot_write_string_0(RWStructuredBuffer<uint32_t> buf_22, uint32_t * offset_21, StructuredBuffer<uint32_t> str_data_1, uint32_t str_len_0)
{

#line 123
    *(&(buf_22)[*offset_21 / 4U]) = str_len_0;
    *offset_21 = *offset_21 + 4U;
    uint32_t _S28 = (str_len_0 + 3U) / 4U;

#line 125
    uint32_t w_0 = 0U;
    for(;;)
    {

#line 126
        if(w_0 < _S28)
        {
        }
        else
        {

#line 126
            break;
        }

#line 127
        *(&(buf_22)[*offset_21 / 4U + w_0]) = str_data_1.Load(w_0);

#line 126
        w_0 = w_0 + 1U;

#line 126
    }


    *offset_21 = *offset_21 + str_len_0;
    return;
}


#line 175
static void godot_write_object_internal_0(RWStructuredBuffer<uint32_t> buf_23, uint32_t * offset_22, StructuredBuffer<uint32_t> path_data_0, uint32_t path_len_0)
{

#line 176
    *(&(buf_23)[*offset_22 / 4U]) = 24U;
    uint32_t _S29 = *offset_22 + 4U;

#line 177
    *offset_22 = _S29;
    *(&(buf_23)[_S29 / 4U]) = 2U;
    uint32_t _S30 = *offset_22 + 4U;

#line 179
    *offset_22 = _S30;
    *(&(buf_23)[_S30 / 4U]) = path_len_0;
    *offset_22 = *offset_22 + 4U;
    uint32_t wc_0 = (path_len_0 + 3U) / 4U;

#line 182
    uint32_t w_1 = 0U;
    for(;;)
    {

#line 183
        if(w_1 < wc_0)
        {
        }
        else
        {

#line 183
            break;
        }

#line 184
        *(&(buf_23)[*offset_22 / 4U + w_1]) = path_data_0.Load(w_1);

#line 183
        w_1 = w_1 + 1U;

#line 183
    }


    *offset_22 = *offset_22 + wc_0 * 4U;
    return;
}


#line 189
static void godot_write_object_ext_index_0(RWStructuredBuffer<uint32_t> buf_24, uint32_t * offset_23, uint32_t idx_0)
{

#line 190
    *(&(buf_24)[*offset_23 / 4U]) = 24U;
    uint32_t _S31 = *offset_23 + 4U;

#line 191
    *offset_23 = _S31;
    *(&(buf_24)[_S31 / 4U]) = 3U;
    uint32_t _S32 = *offset_23 + 4U;

#line 193
    *offset_23 = _S32;
    *(&(buf_24)[_S32 / 4U]) = idx_0;
    *offset_23 = *offset_23 + 4U;
    return;
}


#line 249
static void godot_write_resource_property_count_0(RWStructuredBuffer<uint32_t> buf_25, uint32_t * offset_24, uint32_t count_8)
{

#line 250
    *(&(buf_25)[*offset_24 / 4U]) = count_8;
    *offset_24 = *offset_24 + 4U;
    return;
}


#line 254
static void godot_write_property_name_idx_0(RWStructuredBuffer<uint32_t> buf_26, uint32_t * offset_25, uint32_t idx_1)
{

#line 255
    *(&(buf_26)[*offset_25 / 4U]) = idx_1;
    *offset_25 = *offset_25 + 4U;
    return;
}


#line 213
static void godot_write_footer_0(RWStructuredBuffer<uint32_t> buf_27, uint32_t * offset_26)
{

#line 214
    *(&(buf_27)[*offset_26 / 4U]) = 1129468754U;
    *offset_26 = *offset_26 + 4U;
    return;
}


#line 218
static void godot_write_rscc_header_0(RWStructuredBuffer<uint32_t> buf_28, uint32_t * offset_27, uint32_t mode_0, uint32_t block_size_0, uint32_t total_size_0)
{

#line 219
    *(&(buf_28)[*offset_27 / 4U]) = 1128485714U;
    uint32_t _S33 = *offset_27 + 4U;

#line 220
    *offset_27 = _S33;
    *(&(buf_28)[_S33 / 4U]) = mode_0;
    uint32_t _S34 = *offset_27 + 4U;

#line 222
    *offset_27 = _S34;
    *(&(buf_28)[_S34 / 4U]) = block_size_0;
    uint32_t _S35 = *offset_27 + 4U;

#line 224
    *offset_27 = _S35;
    *(&(buf_28)[_S35 / 4U]) = total_size_0;
    *offset_27 = *offset_27 + 4U;
    return;
}


#line 229
static void godot_write_rscc_footer_0(RWStructuredBuffer<uint32_t> buf_29, uint32_t * offset_28)
{

#line 230
    *(&(buf_29)[*offset_28 / 4U]) = 1128485714U;
    *offset_28 = *offset_28 + 4U;
    return;
}


#line 267
void _bake_scn_kernel(void* _S36, void* entryPointParams_0, void* globalParams_1)
{

#line 267
    ComputeThreadVaryingInput * _S37 = (slang_bit_cast<ComputeThreadVaryingInput *>(_S36));

#line 267
    KernelContext_0 kernelContext_0;

#line 267
    (&kernelContext_0)->globalParams_0 = (slang_bit_cast<GlobalParams_0*>(globalParams_1));
    if(((_S37->groupID + _S37->groupThreadID).x) == 0U)
    {

#line 269
        uint32_t offset_29 = 0U;
        GodotResourceHeader_0 hdr_1;
        (&hdr_1)->big_endian_0 = 0U;
        (&hdr_1)->use_64bit_0 = 0U;
        (&hdr_1)->ver_major_0 = 4U;
        (&hdr_1)->ver_minor_0 = 4U;
        (&hdr_1)->format_version_0 = 6U;
        (&hdr_1)->flags_0 = 0U;
        (&hdr_1)->uid_lo_0 = 0U;
        (&hdr_1)->uid_hi_0 = 0U;

#line 278
        RWStructuredBuffer<uint32_t> _S38 = (&kernelContext_0)->globalParams_0->buf_0;

#line 278
        GodotResourceHeader_0 _S39 = hdr_1;

#line 278
        godot_write_header_0(_S38, &offset_29, &_S39);

        godot_write_reserved_fields_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29);
        godot_write_string_table_header_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_ext_resource_count_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_int_resource_count_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_store_u32_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_store_u64_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U, 0U);
        godot_store_float_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0.0f);
        godot_write_variant_tag_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_nil_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29);
        godot_write_bool_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_int_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, int(0));

#line 290
        Vector<float, 3>  v3_0 = {};


        godot_write_vector3_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, v3_0);

#line 293
        Vector<float, 4>  v4_0 = {};
        godot_write_quaternion_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, v4_0);
        godot_write_color_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, v4_0);
        godot_write_transform3d_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, v3_0, v3_0);
        godot_write_dict_header_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_array_header_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_packed_string_array_header_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_packed_int32_array_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, (&kernelContext_0)->globalParams_0->int_data_0, 0U);
        godot_write_packed_float32_array_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, (&kernelContext_0)->globalParams_0->float_data_0, 0U);
        godot_write_string_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, (&kernelContext_0)->globalParams_0->str_data_0, 0U);
        godot_write_object_internal_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, (&kernelContext_0)->globalParams_0->str_data_0, 0U);
        godot_write_object_ext_index_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_resource_property_count_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_property_name_idx_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 0U);
        godot_write_footer_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29);
        godot_write_rscc_header_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29, 2U, 4096U, 0U);
        godot_write_rscc_footer_0((&kernelContext_0)->globalParams_0->buf_0, &offset_29);

#line 268
    }

#line 311
    return;
}

// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void bake_scn_kernel_Thread(ComputeThreadVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    _bake_scn_kernel(varyingInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void bake_scn_kernel_Group(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeThreadVaryingInput threadInput = {};
    threadInput.groupID = varyingInput->startGroupID;
    _bake_scn_kernel(&threadInput, entryPointParams, globalParams);
}
// [numthreads(1, 1, 1)]
SLANG_PRELUDE_EXPORT
void bake_scn_kernel(ComputeVaryingInput* varyingInput, void* entryPointParams, void* globalParams)
{
    ComputeVaryingInput vi = *varyingInput;
    ComputeVaryingInput groupVaryingInput = {};
    for (uint32_t z = vi.startGroupID.z; z < vi.endGroupID.z; ++z)
    {
        groupVaryingInput.startGroupID.z = z;
        for (uint32_t y = vi.startGroupID.y; y < vi.endGroupID.y; ++y)
        {
            groupVaryingInput.startGroupID.y = y;
            for (uint32_t x = vi.startGroupID.x; x < vi.endGroupID.x; ++x)
            {
                groupVaryingInput.startGroupID.x = x;
                bake_scn_kernel_Group(&groupVaryingInput, entryPointParams, globalParams);
            }
        }
    }
}
