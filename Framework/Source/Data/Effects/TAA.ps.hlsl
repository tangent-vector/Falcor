/***************************************************************************
Copyright (c) 2017, NVIDIA CORPORATION.  All rights reserved.
***************************************************************************/
#include "HostDeviceData.h"

cbuffer PerFrameCB : register(b0)
{
    float gAlpha;
    float gColorBoxSigma;
};

Texture2D gTexColor;
Texture2D gTexMotionVec;
Texture2D gTexPrevColor;
SamplerState gSampler;


// Catmull-Rom filtering code from http://vec3.ca/bicubic-filtering-in-fewer-taps/
float3 bicubicSampleCatmullRom(Texture2D tex, SamplerState samp, float2 samplePos, float2 textureSize)
{
    float2 invTextureSize = 1.0 / textureSize;
    float2 tc = floor(samplePos - 0.5) + 0.5;
    float2 f = samplePos - tc;
    float2 f2 = f * f;
    float2 f3 = f2 * f;

    float2 w0 = f2 - 0.5 * (f3 + f);
    float2 w1 = 1.5 * f3 - 2.5 * f2 + 1;
    float2 w3 = 0.5 * (f3 - f2);
    float2 w2 = 1 - w0 - w1 - w3;

    float2 w12 = w1 + w2;
    
    float2 tc0 = (tc - 1) * invTextureSize;
    float2 tc12 = (tc + w2 / w12) * invTextureSize;
    float2 tc3 = (tc + 2) * invTextureSize;

    float3 result =
        tex.SampleLevel(samp, float2(tc0.x,  tc0.y), 0).rgb  * (w0.x  * w0.y) +
        tex.SampleLevel(samp, float2(tc0.x,  tc12.y), 0).rgb * (w0.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc0.x,  tc3.y), 0).rgb  * (w0.x  * w3.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc0.y), 0).rgb  * (w12.x * w0.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc12.y), 0).rgb * (w12.x * w12.y) +
        tex.SampleLevel(samp, float2(tc12.x, tc3.y), 0).rgb  * (w12.x * w3.y) +
        tex.SampleLevel(samp, float2(tc3.x,  tc0.y), 0).rgb  * (w3.x  * w0.y) +
        tex.SampleLevel(samp, float2(tc3.x,  tc12.y), 0).rgb * (w3.x  * w12.y) +
        tex.SampleLevel(samp, float2(tc3.x,  tc3.y), 0).rgb  * (w3.x  * w3.y);

    return result;
}


float4 main(float2 texC : TEXCOORD) : SV_TARGET0
{
    const int2 offset[8] = { int2(-1, -1), int2(-1,  1),
                              int2( 1, -1), int2( 1,  1), 
                              int2( 1,  0), int2( 0, -1), 
                              int2( 0,  1), int2(-1,  0), };
    
    uint2 textureSize;
    uint levels;
    gTexColor.GetDimensions(0, textureSize.x, textureSize.y, levels);

    const float2 pos = texC * textureSize;
    const int2 ipos = int2(pos);

    // Fetch the current pixel color and compute the color bounding box
    // Details here: http://www.gdcvault.com/play/1023521/From-the-Lab-Bench-Real
    // and here: http://cwyman.org/papers/siga16_gazeTrackedFoveatedRendering.pdf
    float3 color = gTexColor.Load(int3(ipos, 0)).rgb;
    color = RGBToYCgCo(color);
    float3 colorAvg = color;
    float3 colorVar = color * color;
    [unroll]
    for (int k = 0; k < 8; k++) 
    {
        float3 c = gTexColor.Load(int3(ipos + offset[k], 0)).rgb;
        c = RGBToYCgCo(c);
        colorAvg += c;
        colorVar += c * c;
    }

    const float oneOverNine = 1.0 / 9.0;
    colorAvg *= oneOverNine;
    colorVar *= oneOverNine;

    const float3 sigma = sqrt(max(0.0f, colorVar - colorAvg * colorAvg));
    float3 colorMin = colorAvg - gColorBoxSigma * sigma;
    float3 colorMax = colorAvg + gColorBoxSigma * sigma;    

    // Find the longest motion vector
    float2 motion = gTexMotionVec.Load(int3(ipos, 0)).xy;
    [unroll]
    for (int a = 0; a < 8; a++) 
    {
        const float2 m = gTexMotionVec.Load(int3(ipos + offset[a], 0)).rg;
        motion = dot(m, m) > dot(motion, motion) ? m : motion;   
    }

    // Use motion vector to fetch previous frame color (history)
    float3 history = bicubicSampleCatmullRom(gTexPrevColor, gSampler, (texC + motion) * textureSize, textureSize);

    history = RGBToYCgCo(history);

    // Anti-flickering, based on Brian Karis talk @Siggraph 2014
    // https://de45xmedrsdbp.cloudfront.net/Resources/files/TemporalAA_small-59732822.pdf
    // Reduce blend factor when history is near clamping
    float distToClamp = min(abs(colorMin.x - history.x), abs(colorMax.x - history.x));
    float alpha = clamp((gAlpha * distToClamp) / (distToClamp + colorMax.x - colorMin.x), 0.0f, 1.0f);

    history = clamp(history, colorMin, colorMax);
    float3 result = YCgCoToRGB(lerp(history, color, alpha));
    return float4(result, 0);
}
