#include <metal_stdlib>
#include <metal_math>
#include <metal_texture>
using namespace metal;

#line 1 "openusd-fabric/lean/shaders/godot_scn.slang"
struct GodotResourceHeader_0
{
    uint big_endian_0;
    uint use_64bit_0;
    uint ver_major_0;
    uint ver_minor_0;
    uint format_version_0;
    uint flags_0;
    uint uid_lo_0;
    uint uid_hi_0;
};


#line 198
void godot_write_header_0(uint device* buf_0, uint thread* offset_0, const GodotResourceHeader_0 thread* hdr_0)
{

#line 199
    *(buf_0+*offset_0 / 4U) = 1129468754U;
    uint _S1 = *offset_0 + 4U;

#line 200
    *offset_0 = _S1;
    *(buf_0+_S1 / 4U) = hdr_0->big_endian_0;
    uint _S2 = *offset_0 + 4U;

#line 202
    *offset_0 = _S2;
    *(buf_0+_S2 / 4U) = hdr_0->use_64bit_0;
    uint _S3 = *offset_0 + 4U;

#line 204
    *offset_0 = _S3;
    *(buf_0+_S3 / 4U) = hdr_0->ver_major_0;
    uint _S4 = *offset_0 + 4U;

#line 206
    *offset_0 = _S4;
    *(buf_0+_S4 / 4U) = hdr_0->ver_minor_0;
    uint _S5 = *offset_0 + 4U;

#line 208
    *offset_0 = _S5;
    *(buf_0+_S5 / 4U) = hdr_0->format_version_0;
    *offset_0 = *offset_0 + 4U;
    return;
}


#line 259
void godot_write_reserved_fields_0(uint device* buf_1, uint thread* offset_1)
{

#line 259
    uint i_0 = 0U;
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
        *(buf_1+(*offset_1 / 4U + i_0)) = 0U;

#line 260
        i_0 = i_0 + 1U;

#line 260
    }


    *offset_1 = *offset_1 + 44U;
    return;
}


#line 234
void godot_write_string_table_header_0(uint device* buf_2, uint thread* offset_2, uint count_0)
{

#line 235
    *(buf_2+*offset_2 / 4U) = count_0;
    *offset_2 = *offset_2 + 4U;
    return;
}


#line 239
void godot_write_ext_resource_count_0(uint device* buf_3, uint thread* offset_3, uint count_1)
{

#line 240
    *(buf_3+*offset_3 / 4U) = count_1;
    *offset_3 = *offset_3 + 4U;
    return;
}


#line 244
void godot_write_int_resource_count_0(uint device* buf_4, uint thread* offset_4, uint count_2)
{

#line 245
    *(buf_4+*offset_4 / 4U) = count_2;
    *offset_4 = *offset_4 + 4U;
    return;
}


#line 33
void godot_store_u32_0(uint device* buf_5, uint thread* offset_5, uint value_0)
{

#line 34
    *(buf_5+*offset_5 / 4U) = value_0;
    *offset_5 = *offset_5 + 4U;
    return;
}


#line 38
void godot_store_u64_0(uint device* buf_6, uint thread* offset_6, uint value_lo_0, uint value_hi_0)
{

#line 39
    *(buf_6+*offset_6 / 4U) = value_lo_0;
    uint _S6 = *offset_6 + 4U;

#line 40
    *offset_6 = _S6;
    *(buf_6+_S6 / 4U) = value_hi_0;
    *offset_6 = *offset_6 + 4U;
    return;
}


#line 45
void godot_store_float_0(uint device* buf_7, uint thread* offset_7, float value_1)
{

#line 46
    *(buf_7+*offset_7 / 4U) = (as_type<uint>((value_1)));
    *offset_7 = *offset_7 + 4U;
    return;
}


#line 50
void godot_write_variant_tag_0(uint device* buf_8, uint thread* offset_8, uint tag_0)
{

#line 51
    *(buf_8+*offset_8 / 4U) = tag_0;
    *offset_8 = *offset_8 + 4U;
    return;
}


#line 55
void godot_write_nil_0(uint device* buf_9, uint thread* offset_9)
{

#line 56
    *(buf_9+*offset_9 / 4U) = 1U;
    *offset_9 = *offset_9 + 4U;
    return;
}


#line 60
void godot_write_bool_0(uint device* buf_10, uint thread* offset_10, uint value_2)
{

#line 61
    *(buf_10+*offset_10 / 4U) = 2U;
    uint _S7 = *offset_10 + 4U;

#line 62
    *offset_10 = _S7;
    *(buf_10+_S7 / 4U) = value_2;
    *offset_10 = *offset_10 + 4U;
    return;
}


#line 67
void godot_write_int_0(uint device* buf_11, uint thread* offset_11, int value_3)
{

#line 68
    *(buf_11+*offset_11 / 4U) = 3U;
    uint _S8 = *offset_11 + 4U;

#line 69
    *offset_11 = _S8;
    *(buf_11+_S8 / 4U) = (as_type<uint>((value_3)));
    *offset_11 = *offset_11 + 4U;
    return;
}


#line 74
void godot_write_vector3_0(uint device* buf_12, uint thread* offset_12, float3 v_0)
{

#line 75
    *(buf_12+*offset_12 / 4U) = 12U;
    uint _S9 = *offset_12 + 4U;

#line 76
    *offset_12 = _S9;
    *(buf_12+_S9 / 4U) = (as_type<uint>((v_0.x)));
    uint _S10 = *offset_12 + 4U;

#line 78
    *offset_12 = _S10;
    *(buf_12+_S10 / 4U) = (as_type<uint>((v_0.y)));
    uint _S11 = *offset_12 + 4U;

#line 80
    *offset_12 = _S11;
    *(buf_12+_S11 / 4U) = (as_type<uint>((v_0.z)));
    *offset_12 = *offset_12 + 4U;
    return;
}


#line 85
void godot_write_quaternion_0(uint device* buf_13, uint thread* offset_13, float4 q_0)
{

#line 86
    *(buf_13+*offset_13 / 4U) = 14U;
    uint _S12 = *offset_13 + 4U;

#line 87
    *offset_13 = _S12;
    *(buf_13+_S12 / 4U) = (as_type<uint>((q_0.x)));
    uint _S13 = *offset_13 + 4U;

#line 89
    *offset_13 = _S13;
    *(buf_13+_S13 / 4U) = (as_type<uint>((q_0.y)));
    uint _S14 = *offset_13 + 4U;

#line 91
    *offset_13 = _S14;
    *(buf_13+_S14 / 4U) = (as_type<uint>((q_0.z)));
    uint _S15 = *offset_13 + 4U;

#line 93
    *offset_13 = _S15;
    *(buf_13+_S15 / 4U) = (as_type<uint>((q_0.w)));
    *offset_13 = *offset_13 + 4U;
    return;
}


#line 98
void godot_write_color_0(uint device* buf_14, uint thread* offset_14, float4 c_0)
{

#line 99
    *(buf_14+*offset_14 / 4U) = 20U;
    uint _S16 = *offset_14 + 4U;

#line 100
    *offset_14 = _S16;
    *(buf_14+_S16 / 4U) = (as_type<uint>((c_0.x)));
    uint _S17 = *offset_14 + 4U;

#line 102
    *offset_14 = _S17;
    *(buf_14+_S17 / 4U) = (as_type<uint>((c_0.y)));
    uint _S18 = *offset_14 + 4U;

#line 104
    *offset_14 = _S18;
    *(buf_14+_S18 / 4U) = (as_type<uint>((c_0.z)));
    uint _S19 = *offset_14 + 4U;

#line 106
    *offset_14 = _S19;
    *(buf_14+_S19 / 4U) = (as_type<uint>((c_0.w)));
    *offset_14 = *offset_14 + 4U;
    return;
}


#line 111
void godot_write_transform3d_0(uint device* buf_15, uint thread* offset_15, float3 basis_0, float3 origin_0)
{

#line 112
    *(buf_15+*offset_15 / 4U) = 17U;
    uint _S20 = *offset_15 + 4U;

#line 113
    *offset_15 = _S20;
    *(buf_15+_S20 / 4U) = (as_type<uint>((origin_0.x)));
    uint _S21 = *offset_15 + 4U;

#line 115
    *offset_15 = _S21;
    *(buf_15+_S21 / 4U) = (as_type<uint>((origin_0.y)));
    uint _S22 = *offset_15 + 4U;

#line 117
    *offset_15 = _S22;
    *(buf_15+_S22 / 4U) = (as_type<uint>((origin_0.z)));
    *offset_15 = *offset_15 + 4U;
    return;
}


#line 132
void godot_write_dict_header_0(uint device* buf_16, uint thread* offset_16, uint count_3)
{

#line 133
    *(buf_16+*offset_16 / 4U) = 26U;
    uint _S23 = *offset_16 + 4U;

#line 134
    *offset_16 = _S23;
    *(buf_16+_S23 / 4U) = count_3;
    *offset_16 = *offset_16 + 4U;
    return;
}


#line 139
void godot_write_array_header_0(uint device* buf_17, uint thread* offset_17, uint count_4)
{

#line 140
    *(buf_17+*offset_17 / 4U) = 30U;
    uint _S24 = *offset_17 + 4U;

#line 141
    *offset_17 = _S24;
    *(buf_17+_S24 / 4U) = count_4;
    *offset_17 = *offset_17 + 4U;
    return;
}


#line 168
void godot_write_packed_string_array_header_0(uint device* buf_18, uint thread* offset_18, uint count_5)
{

#line 169
    *(buf_18+*offset_18 / 4U) = 34U;
    uint _S25 = *offset_18 + 4U;

#line 170
    *offset_18 = _S25;
    *(buf_18+_S25 / 4U) = count_5;
    *offset_18 = *offset_18 + 4U;
    return;
}


#line 146
void godot_write_packed_int32_array_0(uint device* buf_19, uint thread* offset_19, uint device* data_0, uint count_6)
{

#line 147
    *(buf_19+*offset_19 / 4U) = 32U;
    uint _S26 = *offset_19 + 4U;

#line 148
    *offset_19 = _S26;
    *(buf_19+_S26 / 4U) = count_6;
    *offset_19 = *offset_19 + 4U;

#line 150
    uint i_1 = 0U;
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
        *(buf_19+(*offset_19 / 4U + i_1)) = data_0[i_1];

#line 151
        i_1 = i_1 + 1U;

#line 151
    }


    *offset_19 = *offset_19 + count_6 * 4U;
    return;
}


#line 157
void godot_write_packed_float32_array_0(uint device* buf_20, uint thread* offset_20, float device* data_1, uint count_7)
{

#line 158
    *(buf_20+*offset_20 / 4U) = 33U;
    uint _S27 = *offset_20 + 4U;

#line 159
    *offset_20 = _S27;
    *(buf_20+_S27 / 4U) = count_7;
    *offset_20 = *offset_20 + 4U;

#line 161
    uint i_2 = 0U;
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
        *(buf_20+(*offset_20 / 4U + i_2)) = (as_type<uint>((data_1[i_2])));

#line 162
        i_2 = i_2 + 1U;

#line 162
    }


    *offset_20 = *offset_20 + count_7 * 4U;
    return;
}


#line 122
void godot_write_string_0(uint device* buf_21, uint thread* offset_21, uint device* str_data_0, uint str_len_0)
{

#line 123
    *(buf_21+*offset_21 / 4U) = str_len_0;
    *offset_21 = *offset_21 + 4U;
    uint _S28 = (str_len_0 + 3U) / 4U;

#line 125
    uint w_0 = 0U;
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
        *(buf_21+(*offset_21 / 4U + w_0)) = str_data_0[w_0];

#line 126
        w_0 = w_0 + 1U;

#line 126
    }


    *offset_21 = *offset_21 + str_len_0;
    return;
}


#line 175
void godot_write_object_internal_0(uint device* buf_22, uint thread* offset_22, uint device* path_data_0, uint path_len_0)
{

#line 176
    *(buf_22+*offset_22 / 4U) = 24U;
    uint _S29 = *offset_22 + 4U;

#line 177
    *offset_22 = _S29;
    *(buf_22+_S29 / 4U) = 2U;
    uint _S30 = *offset_22 + 4U;

#line 179
    *offset_22 = _S30;
    *(buf_22+_S30 / 4U) = path_len_0;
    *offset_22 = *offset_22 + 4U;
    uint wc_0 = (path_len_0 + 3U) / 4U;

#line 182
    uint w_1 = 0U;
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
        *(buf_22+(*offset_22 / 4U + w_1)) = path_data_0[w_1];

#line 183
        w_1 = w_1 + 1U;

#line 183
    }


    *offset_22 = *offset_22 + wc_0 * 4U;
    return;
}


#line 189
void godot_write_object_ext_index_0(uint device* buf_23, uint thread* offset_23, uint idx_0)
{

#line 190
    *(buf_23+*offset_23 / 4U) = 24U;
    uint _S31 = *offset_23 + 4U;

#line 191
    *offset_23 = _S31;
    *(buf_23+_S31 / 4U) = 3U;
    uint _S32 = *offset_23 + 4U;

#line 193
    *offset_23 = _S32;
    *(buf_23+_S32 / 4U) = idx_0;
    *offset_23 = *offset_23 + 4U;
    return;
}


#line 249
void godot_write_resource_property_count_0(uint device* buf_24, uint thread* offset_24, uint count_8)
{

#line 250
    *(buf_24+*offset_24 / 4U) = count_8;
    *offset_24 = *offset_24 + 4U;
    return;
}


#line 254
void godot_write_property_name_idx_0(uint device* buf_25, uint thread* offset_25, uint idx_1)
{

#line 255
    *(buf_25+*offset_25 / 4U) = idx_1;
    *offset_25 = *offset_25 + 4U;
    return;
}


#line 213
void godot_write_footer_0(uint device* buf_26, uint thread* offset_26)
{

#line 214
    *(buf_26+*offset_26 / 4U) = 1129468754U;
    *offset_26 = *offset_26 + 4U;
    return;
}


#line 218
void godot_write_rscc_header_0(uint device* buf_27, uint thread* offset_27, uint mode_0, uint block_size_0, uint total_size_0)
{

#line 219
    *(buf_27+*offset_27 / 4U) = 1128485714U;
    uint _S33 = *offset_27 + 4U;

#line 220
    *offset_27 = _S33;
    *(buf_27+_S33 / 4U) = mode_0;
    uint _S34 = *offset_27 + 4U;

#line 222
    *offset_27 = _S34;
    *(buf_27+_S34 / 4U) = block_size_0;
    uint _S35 = *offset_27 + 4U;

#line 224
    *offset_27 = _S35;
    *(buf_27+_S35 / 4U) = total_size_0;
    *offset_27 = *offset_27 + 4U;
    return;
}


#line 229
void godot_write_rscc_footer_0(uint device* buf_28, uint thread* offset_28)
{

#line 230
    *(buf_28+*offset_28 / 4U) = 1128485714U;
    *offset_28 = *offset_28 + 4U;
    return;
}


#line 267
struct KernelContext_0
{
    uint device* buf_29;
    uint device* int_data_0;
    float device* float_data_0;
    uint device* str_data_1;
};


#line 267
[[kernel]] void bake_scn_kernel(uint3 tid_0 [[thread_position_in_grid]], uint device* buf_30 [[buffer(0)]], uint device* int_data_1 [[buffer(2)]], float device* float_data_1 [[buffer(3)]], uint device* str_data_2 [[buffer(1)]])
{

#line 267
    thread KernelContext_0 kernelContext_0;

#line 267
    (&kernelContext_0)->buf_29 = buf_30;

#line 267
    (&kernelContext_0)->int_data_0 = int_data_1;

#line 267
    (&kernelContext_0)->float_data_0 = float_data_1;

#line 267
    (&kernelContext_0)->str_data_1 = str_data_2;
    if((tid_0.x) == 0U)
    {

#line 269
        thread uint offset_29 = 0U;
        thread GodotResourceHeader_0 hdr_1;
        (&hdr_1)->big_endian_0 = 0U;
        (&hdr_1)->use_64bit_0 = 0U;
        (&hdr_1)->ver_major_0 = 4U;
        (&hdr_1)->ver_minor_0 = 4U;
        (&hdr_1)->format_version_0 = 6U;
        (&hdr_1)->flags_0 = 0U;
        (&hdr_1)->uid_lo_0 = 0U;
        (&hdr_1)->uid_hi_0 = 0U;

#line 278
        thread GodotResourceHeader_0 _S36 = hdr_1;

#line 278
        godot_write_header_0((&kernelContext_0)->buf_29, &offset_29, &_S36);

        godot_write_reserved_fields_0((&kernelContext_0)->buf_29, &offset_29);
        godot_write_string_table_header_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_ext_resource_count_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_int_resource_count_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_store_u32_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_store_u64_0((&kernelContext_0)->buf_29, &offset_29, 0U, 0U);
        godot_store_float_0((&kernelContext_0)->buf_29, &offset_29, 0.0);
        godot_write_variant_tag_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_nil_0((&kernelContext_0)->buf_29, &offset_29);
        godot_write_bool_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_int_0((&kernelContext_0)->buf_29, &offset_29, int(0));

#line 290
        float3 v3_0;


        godot_write_vector3_0((&kernelContext_0)->buf_29, &offset_29, v3_0);

#line 293
        float4 v4_0;
        godot_write_quaternion_0((&kernelContext_0)->buf_29, &offset_29, v4_0);
        godot_write_color_0((&kernelContext_0)->buf_29, &offset_29, v4_0);
        godot_write_transform3d_0((&kernelContext_0)->buf_29, &offset_29, v3_0, v3_0);
        godot_write_dict_header_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_array_header_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_packed_string_array_header_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_packed_int32_array_0((&kernelContext_0)->buf_29, &offset_29, (&kernelContext_0)->int_data_0, 0U);
        godot_write_packed_float32_array_0((&kernelContext_0)->buf_29, &offset_29, (&kernelContext_0)->float_data_0, 0U);
        godot_write_string_0((&kernelContext_0)->buf_29, &offset_29, (&kernelContext_0)->str_data_1, 0U);
        godot_write_object_internal_0((&kernelContext_0)->buf_29, &offset_29, (&kernelContext_0)->str_data_1, 0U);
        godot_write_object_ext_index_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_resource_property_count_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_property_name_idx_0((&kernelContext_0)->buf_29, &offset_29, 0U);
        godot_write_footer_0((&kernelContext_0)->buf_29, &offset_29);
        godot_write_rscc_header_0((&kernelContext_0)->buf_29, &offset_29, 2U, 4096U, 0U);
        godot_write_rscc_footer_0((&kernelContext_0)->buf_29, &offset_29);

#line 268
    }

#line 311
    return;
}

