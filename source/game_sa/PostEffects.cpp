#include "StdInc.h"

#include "PostEffects.h"
#include "CustomBuildingDNPipeline.h"
#include "Clouds.h"

RwIm2DVertex (&cc_vertices)[4] = *(RwIm2DVertex(*)[4])0xC400D8;
RwImVertexIndex (&cc_indices)[12] = *(RwImVertexIndex(*)[12])0x8D5174; // { 0, 1, 2, 0, 2, 3, 0, 1, 2, 0, 2, 3 };

int32 (&hpX)[180] = *(int32(*)[180])0xC3FE08;
int32 (&hpY)[180] = *(int32(*)[180])0xC3FB38;
int32 (&hpS)[180] = *(int32(*)[180])0xC3F868; // speed

static inline float& s_DayNightBalanceParamOld = StaticRef<float>(0xC3F860);

static constexpr auto GRAIN_TEXTURE_DIM = 256u; // 256x256

void CPostEffects::InjectHooks() {
    RH_ScopedClass(CPostEffects);
    RH_ScopedCategoryGlobal();

    RH_ScopedInstall(Initialise, 0x704630);
    RH_ScopedInstall(Close, 0x7010C0);
    RH_ScopedInstall(DoScreenModeDependentInitializations, 0x7046D0);
    RH_ScopedInstall(SetupBackBufferVertex, 0x7043D0);
    RH_ScopedInstall(Update, 0x7046A0);
    RH_ScopedInstall(DrawQuad, 0x700EC0);
    RH_ScopedInstall(FilterFX_StoreAndSetDayNightBalance, 0x7034B0);
    RH_ScopedInstall(FilterFX_RestoreDayNightBalance, 0x7034D0);
    RH_ScopedInstall(ImmediateModeFilterStuffInitialize, 0x703CC0);
    RH_ScopedInstall(ImmediateModeRenderStatesSet, 0x700D70);
    RH_ScopedInstall(ImmediateModeRenderStatesStore, 0x700CC0, {.locked = true}); // EAX is overriden when unhooked for some reason
    RH_ScopedInstall(ImmediateModeRenderStatesReStore, 0x700E00);
    RH_ScopedInstall(ScriptCCTVSwitch, 0x7011B0);
    RH_ScopedInstall(ScriptDarknessFilterSwitch, 0x701170);
    RH_ScopedInstall(ScriptHeatHazeFXSwitch, 0x701160);
    RH_ScopedInstall(ScriptInfraredVisionSwitch, 0x701140);
    RH_ScopedInstall(ScriptNightVisionSwitch, 0x701120);
    RH_ScopedInstall(ScriptResetForEffects, 0x7010F0);
    RH_ScopedInstall(UnderWaterRipple, 0x7039C0);
    RH_ScopedInstall(HeatHazeFXInit, 0x701450);
    RH_ScopedInstall(HeatHazeFX, 0x701780, { .reversed = false });
    RH_ScopedInstall(IsVisionFXActive, 0x7034F0);
    RH_ScopedInstall(NightVision, 0x7011C0);
    RH_ScopedInstall(NightVisionSetLights, 0x7012E0);
    RH_ScopedInstall(SetFilterMainColour, 0x703520);
    RH_ScopedInstall(InfraredVision, 0x703F80);
    RH_ScopedInstall(InfraredVisionSetLightsForDefaultObjects, 0x701430);
    RH_ScopedInstall(InfraredVisionStoreAndSetLightsForHeatObjects, 0x701320);
    RH_ScopedInstall(InfraredVisionRestoreLightsForHeatObjects, 0x701410);
    RH_ScopedInstall(Fog, 0x704150);
    RH_ScopedInstall(CCTV, 0x702F40);
    RH_ScopedInstall(Grain, 0x7037C0);
    RH_ScopedInstall(SpeedFX, 0x7030A0, { .reversed = false });
    RH_ScopedInstall(DarknessFilter, 0x702F00);
    RH_ScopedInstall(ColourFilter, 0x703650);
    RH_ScopedInstall(Radiosity, 0x702080, { .reversed = false });
    RH_ScopedInstall(SetSpeedFXManualSpeedCurrentFrame, 0x700BE0);
    RH_ScopedInstall(Render, 0x7046E0);
}

// 0x704630
void CPostEffects::Initialise() {
    SetupBackBufferVertex();
    if (m_pGrainRaster = RwRasterCreate(GRAIN_TEXTURE_DIM, GRAIN_TEXTURE_DIM, 32, rwRASTERFORMAT8888 | rwRASTERTYPETEXTURE)) {
        auto* pixels = RwRasterLock(m_pGrainRaster, 0, rwRASTERLOCKWRITE);
        for (auto i = 0u; i < sq(GRAIN_TEXTURE_DIM); i += 4) {
            pixels[i + 0] = pixels[i + 1] = pixels[i + 2] = pixels[i + 3] = static_cast<uint8>(CGeneral::GetRandomNumber());
        }

        RwRasterUnlock(m_pGrainRaster);
    }
}

// 0x7010C0
void CPostEffects::Close() {
    RwRasterDestroy(m_pGrainRaster);
    if (pRasterFrontBuffer) {
        RwRasterDestroy(std::exchange(pRasterFrontBuffer, nullptr));
    }
}

// 0x7046D0
void CPostEffects::DoScreenModeDependentInitializations() {
    ImmediateModeFilterStuffInitialize();
    HeatHazeFXInit();
}

// NOTSA: Returns the next power of 2 greater than or equal to n.
static uint32 GetNextPow2(uint32 n) {
    if (n == 0) {
        return 1;
    }

    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;

    return n + 1;
}

// 0x7043D0
void CPostEffects::SetupBackBufferVertex() {
    RwRaster* raster = RwCameraGetRaster(Scene.m_pRwCamera);

    // get maximum 2^N dimensions
    const auto width   = GetNextPow2(RwRasterGetWidth(raster));
    const auto height  = GetNextPow2(RwRasterGetHeight(raster));
    const auto fwidth  = float(width);
    const auto fheight = float(height);

    if (pRasterFrontBuffer && (width != RwRasterGetWidth(pRasterFrontBuffer) || height != RwRasterGetHeight(pRasterFrontBuffer))) {
        RwRasterDestroy(std::exchange(pRasterFrontBuffer, nullptr));
    }
    if (!pRasterFrontBuffer) {
        pRasterFrontBuffer = RasterCreatePostEffects(RwRect{ .w = (int32)width, .h = (int32)height });

        if (!pRasterFrontBuffer) {
            NOTSA_LOG_ERR("Error subrastering");
        }
    }

    cc_vertices[0].x   = 0.0f;
    cc_vertices[0].y   = 0.0f;
    cc_vertices[0].z   = RwIm2DGetNearScreenZ();
    cc_vertices[0].rhw = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);
    cc_vertices[0].u   = 0.5f / fwidth;
    cc_vertices[0].v   = 0.5f / fheight;

    cc_vertices[1].x   = 0.0f;
    cc_vertices[1].y   = fheight;
    cc_vertices[1].z   = RwIm2DGetNearScreenZ();
    cc_vertices[1].rhw = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);
    cc_vertices[1].u   = 0.5f / fwidth;
    cc_vertices[1].v   = (fheight + 0.5f) / fheight;

    cc_vertices[2].x   = fwidth;
    cc_vertices[2].y   = fheight;
    cc_vertices[2].z   = RwIm2DGetNearScreenZ();
    cc_vertices[2].rhw = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);
    cc_vertices[2].u   = (fwidth + 0.5f) / fwidth;
    cc_vertices[2].v   = (fheight + 0.5f) / fheight;

    cc_vertices[3].x   = fwidth;
    cc_vertices[3].y   = 0.0f;
    cc_vertices[3].z   = RwIm2DGetNearScreenZ();
    cc_vertices[3].rhw = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);
    cc_vertices[3].u   = (fwidth + 0.5f) / fwidth;
    cc_vertices[3].v   = 0.5f / fheight;

    if (pRasterFrontBuffer) {
        DoScreenModeDependentInitializations();
    }
}

// 0x7046A0
void CPostEffects::Update() {
    ZoneScoped;

    m_bRainEnable = CWeather::Rain > 0.0f;
    if (!pRasterFrontBuffer) {
        SetupBackBufferVertex();
    }
}

// 0x700EC0
void CPostEffects::DrawQuad(float x1, float y1, float x2, float y2, uint8 red, uint8 green, uint8 blue, uint8 alpha, RwRaster* raster) {
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, raster);

    const auto color = CRGBA(red, green, blue, alpha).ToIntARGB();

    ms_imf.quad[0].x = x1;
    ms_imf.quad[0].y = y1;
    ms_imf.quad[0].z = ms_imf.screenZ;
    ms_imf.quad[0].emissiveColor = color;

    ms_imf.quad[1].x = x1 + x2;
    ms_imf.quad[1].y = y1;
    ms_imf.quad[1].z = ms_imf.screenZ;
    ms_imf.quad[1].emissiveColor = color;

    ms_imf.quad[2].x = x1;
    ms_imf.quad[2].y = y1 + y2;
    ms_imf.quad[2].z = ms_imf.screenZ;
    ms_imf.quad[2].emissiveColor = color;

    ms_imf.quad[3].x = x1 + x2;
    ms_imf.quad[3].y = y1 + y2;
    ms_imf.quad[3].z = ms_imf.screenZ;
    ms_imf.quad[3].emissiveColor = color;

    RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, ms_imf.quad.data(), 4);
}

// 0x701060
void CPostEffects::DrawQuadSetDefaultUVs() {
    DrawQuadSetUVs(0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
}

// 0x700F90
void CPostEffects::DrawQuadSetUVs(float u1, float v1, float u2, float v2, float u3, float v3, float u4, float v4) {
    ms_imf.quad[0].u = u1;
    ms_imf.quad[0].v = v1;
    ms_imf.quad[1].u = u2;
    ms_imf.quad[1].v = v2;
    ms_imf.quad[2].u = u3;
    ms_imf.quad[2].v = v3;
    ms_imf.quad[3].u = u4;
    ms_imf.quad[3].v = v4;
}

// 0x700FE0
void CPostEffects::DrawQuadSetPixelUVs(float u0, float v0, float u1, float v1, float u3, float v3, float u2, float v2) {
    const float x = 1.0f / ms_imf.sizeDrawBufferX;
    const float y = 1.0f / ms_imf.sizeDrawBufferY;

    ms_imf.quad[0].u = x * u0;
    ms_imf.quad[0].v = y * v0;

    ms_imf.quad[1].u = x * u1;
    ms_imf.quad[1].v = y * v1;

    ms_imf.quad[2].u = x * u2;
    ms_imf.quad[2].v = y * v2;

    ms_imf.quad[3].u = x * u3;
    ms_imf.quad[3].v = y * v3;
}

// 0x7034B0
void CPostEffects::FilterFX_StoreAndSetDayNightBalance() {
    if (!m_bInCutscene) {
        s_DayNightBalanceParamOld = CCustomBuildingDNPipeline::m_fDNBalanceParam;
        CCustomBuildingDNPipeline::m_fDNBalanceParam = m_VisionFXDayNightBalance;
    }
}

// 0x7034D0
void CPostEffects::FilterFX_RestoreDayNightBalance() {
    if (!m_bInCutscene) {
        CCustomBuildingDNPipeline::m_fDNBalanceParam = s_DayNightBalanceParamOld;
    }
}

// 0x703CC0
void CPostEffects::ImmediateModeFilterStuffInitialize() {
    ms_imf.screenZ          = RwIm2DGetNearScreenZ();
    ms_imf.RasterDrawBuffer = pRasterFrontBuffer;
    ms_imf.uMinTri          = 0.0f;
    ms_imf.vMinTri          = 0.0f;
    ms_imf.uMaxTri          = 2.0f;
    ms_imf.vMaxTri          = 2.0f;
    ms_imf.recipCameraZ     = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);
    ms_imf.sizeDrawBufferX  = RwRasterGetWidth(ms_imf.RasterDrawBuffer);
    ms_imf.sizeDrawBufferY  = RwRasterGetHeight(ms_imf.RasterDrawBuffer);

    ms_imf.triangle[0] = {
        .x   = 0.0f,
        .y   = 0.0f,
        .z   = ms_imf.screenZ,
        .rhw = ms_imf.recipCameraZ,
        .u   = 0.0f,
        .v   = 0.0f
    };
    ms_imf.triangle[1] = {
        .x   = 2.0f * (float)ms_imf.sizeDrawBufferX,
        .y   = 0.0f,
        .z   = ms_imf.screenZ,
        .rhw = ms_imf.recipCameraZ,
        .u   = 2.0f,
        .v   = 0.0f
    };
    ms_imf.triangle[2] = {
        .x   = 0.0f,
        .y   = 2.0f * (float)ms_imf.sizeDrawBufferY,
        .z   = ms_imf.screenZ,
        .rhw = ms_imf.recipCameraZ,
        .u   = 0.0f,
        .v   = 2.0f
    };

    constexpr auto DEFAULT_QUAD_COLOR = 0xFF00C800;

    ms_imf.quad[0] = {
        .x             = 0.0f,
        .y             = 0.0f,
        .z             = ms_imf.screenZ,
        .rhw           = ms_imf.recipCameraZ,
        .emissiveColor = DEFAULT_QUAD_COLOR,
        .u             = 0.0f,
        .v             = 0.0f
    };
    ms_imf.quad[1] = {
        .x             = 0.0f,
        .y             = 0.0f,
        .z             = ms_imf.screenZ,
        .rhw           = ms_imf.recipCameraZ,
        .emissiveColor = DEFAULT_QUAD_COLOR,
        .u             = 1.0f,
        .v             = 0.0f
    };
    ms_imf.quad[2] = {
        .x             = 0.0f,
        .y             = 0.0f,
        .z             = ms_imf.screenZ,
        .rhw           = ms_imf.recipCameraZ,
        .emissiveColor = DEFAULT_QUAD_COLOR,
        .u             = 0.0f,
        .v             = 1.0f
    };
    ms_imf.quad[3] = {
        .x             = 0.0f,
        .y             = 0.0f,
        .z             = ms_imf.screenZ,
        .rhw           = ms_imf.recipCameraZ,
        .emissiveColor = DEFAULT_QUAD_COLOR,
        .u             = 1.0f,
        .v             = 1.0f
    };

    const auto* frameBuffer = RwCameraGetRaster(Scene.m_pRwCamera);
    ms_imf.fFrontBufferU1 = ms_imf.fFrontBufferV1 = 0.0f;
    ms_imf.fFrontBufferU2 = SCREEN_WIDTH / GetNextPow2(RwRasterGetWidth(frameBuffer));
    ms_imf.fFrontBufferV2 = SCREEN_HEIGHT / GetNextPow2(RwRasterGetHeight(frameBuffer));
}

// 0x700D70
void CPostEffects::ImmediateModeRenderStatesSet() {
    RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATECULLMODE,          RWRSTATE(rwCULLMODECULLNONE));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATESHADEMODE,         RWRSTATE(rwSHADEMODEGOURAUD));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS,    RWRSTATE(rwTEXTUREADDRESSCLAMP));
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERNEAREST));
}

// 0x700CC0
void CPostEffects::ImmediateModeRenderStatesStore() {
    RwRenderStateGet(rwRENDERSTATESRCBLEND,          &ms_imf.blendSrc);
    RwRenderStateGet(rwRENDERSTATEDESTBLEND,         &ms_imf.blendDst);
    RwRenderStateGet(rwRENDERSTATEFOGENABLE,         &ms_imf.bFog);
    RwRenderStateGet(rwRENDERSTATECULLMODE,          &ms_imf.cullMode);
    RwRenderStateGet(rwRENDERSTATEZTESTENABLE,       &ms_imf.bZTest);
    RwRenderStateGet(rwRENDERSTATEZWRITEENABLE,      &ms_imf.bZWrite);
    RwRenderStateGet(rwRENDERSTATESHADEMODE,         &ms_imf.shadeMode);
    RwRenderStateGet(rwRENDERSTATEVERTEXALPHAENABLE, &ms_imf.bVertexAlpha);
    RwRenderStateGet(rwRENDERSTATETEXTUREADDRESS,    &ms_imf.textureAddress);
    RwRenderStateGet(rwRENDERSTATETEXTUREFILTER,     &ms_imf.textureFilter);
}

// 0x700E00
void CPostEffects::ImmediateModeRenderStatesReStore() {
    RwRenderStateSet(rwRENDERSTATESRCBLEND,           RWRSTATE(ms_imf.blendSrc));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,          RWRSTATE(ms_imf.blendDst));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE,          RWRSTATE(ms_imf.bFog));
    RwRenderStateSet(rwRENDERSTATECULLMODE,           RWRSTATE(ms_imf.cullMode));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,        RWRSTATE(ms_imf.bZTest));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,       RWRSTATE(ms_imf.bZWrite));
    RwRenderStateSet(rwRENDERSTATESHADEMODE,          RWRSTATE(ms_imf.shadeMode));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE,  RWRSTATE(ms_imf.bVertexAlpha));
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS,     RWRSTATE(ms_imf.textureAddress));
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,      RWRSTATE(ms_imf.textureFilter));
}

// 0x700C90
RwRaster* CPostEffects::RasterCreatePostEffects(RwRect rect) {
    return RwRasterCreate(rect.w, rect.h, RwRasterGetDepth(RwCameraGetRaster(Scene.m_pRwCamera)), rwRASTERTYPEZBUFFER | rwRASTERTYPETEXTURE);
}

// 0x7011B0
void CPostEffects::ScriptCCTVSwitch(bool enable) {
    m_bCCTV = enable;
}

// 0x701170
void CPostEffects::ScriptDarknessFilterSwitch(bool enable, int32 alpha) {
    m_bDarknessFilter = enable;
    if (alpha == 255) {
        m_DarknessFilterAlpha = m_DarknessFilterAlphaDefault;
    } else {
        m_DarknessFilterAlpha = std::clamp(0, alpha, 255);
    }
}

// 0x701160
void CPostEffects::ScriptHeatHazeFXSwitch(bool enable) {
    m_bHeatHazeFX = enable;
}

// 0x701140
void CPostEffects::ScriptInfraredVisionSwitch(bool enable) {
    if (enable) {
        m_bInfraredVision = true;
        m_bNightVision = false;
    } else {
        m_bInfraredVision = false;
    }
}

// 0x701120
void CPostEffects::ScriptNightVisionSwitch(bool enable) {
    if (enable) {
        m_bNightVision = true;
        m_bInfraredVision = false;
    } else {
        m_bNightVision = false;
    }
}

// 0x7010F0
void CPostEffects::ScriptResetForEffects() {
    m_bNightVision = false;
    m_bInfraredVision = false;
    m_bHeatHazeFX = false;
    m_bDarknessFilter = false;
    m_bCCTV = false;
    CWaterLevel::m_bWaterFogScript = true;
}

// 0x7039C0
void CPostEffects::UnderWaterRipple(CRGBA color, float xoffset, float yoffset, float strength, float speed, float freq) {
    color.a = 255;

    RwCameraEndUpdate(Scene.m_pRwCamera);
    RsCameraBeginUpdate(Scene.m_pRwCamera);
    uiTempBufferVerticesStored = uiTempBufferIndicesStored = 0;
    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);

    const auto rasterWidth   = RwRasterGetWidth(CPostEffects::pRasterFrontBuffer);
    const auto rasterHeight  = RwRasterGetHeight(CPostEffects::pRasterFrontBuffer);
    const auto fRasterWidth  = float(rasterWidth);
    const auto fRasterHeight = float(rasterHeight);
    const auto recipNearClip = 1.0f / RwCameraGetNearClipPlane(Scene.m_pRwCamera);

    const auto EmitVertex = [&](float wave, int32 y) {
        const auto i = uiTempBufferVerticesStored;

        TempBufferVertices.m_2d[i].x             = 0.0f;
        TempBufferVertices.m_2d[i].y             = float(y);
        TempBufferVertices.m_2d[i].z             = RwIm2DGetNearScreenZ();
        TempBufferVertices.m_2d[i].rhw           = recipNearClip;
        TempBufferVertices.m_2d[i].u             = (wave + xoffset) / fRasterWidth;
        TempBufferVertices.m_2d[i].v             = float(y) / fRasterHeight;
        TempBufferVertices.m_2d[i].emissiveColor = color.ToIntABGR();

        TempBufferVertices.m_2d[i + 1].x             = float(int32(2.0f * xoffset) + rasterWidth);
        TempBufferVertices.m_2d[i + 1].y             = float(y);
        TempBufferVertices.m_2d[i + 1].z             = RwIm2DGetNearScreenZ();
        TempBufferVertices.m_2d[i + 1].rhw           = recipNearClip;
        TempBufferVertices.m_2d[i + 1].u             = (fRasterWidth + wave - xoffset) / fRasterWidth;
        TempBufferVertices.m_2d[i + 1].v             = float(y) / fRasterHeight;
        TempBufferVertices.m_2d[i + 1].emissiveColor = color.ToIntABGR();

        uiTempBufferVerticesStored += 2;
    };

    if (rasterHeight > 0) {
        auto y = 0;
        for (; y < rasterHeight; y = int32((float)y + yoffset)) {
            EmitVertex(std::sin(freq * (float)y + speed * (float)CTimer::GetTimeInMS()) * xoffset, y);
        }
        EmitVertex(std::sin(freq * (float)y + speed * (float)CTimer::GetTimeInMS()) * xoffset, y);
    } else {
        // unexpected path?
        EmitVertex(std::sin(speed * (float)CTimer::GetTimeInMS()) * xoffset, 0);
    }

    if (uiTempBufferVerticesStored > 2) {
        RwIm2DRenderPrimitive(rwPRIMTYPETRISTRIP, TempBufferVertices.m_2d, uiTempBufferVerticesStored);
    }
    ImmediateModeRenderStatesReStore();
}

// unused
// 0x703CB0
void CPostEffects::UnderWaterRippleFadeToFX() {
    // NOP
}

// 0x701450
void CPostEffects::HeatHazeFXInit() {
    // m_HeatHazeFXType always HEAT_HAZE_0
    if (m_HeatHazeFXType == m_HeatHazeFXTypeLast)
        return;

    switch (m_HeatHazeFXType) {
    case HEAT_HAZE_0:
        m_HeatHazeFXIntensity   = 80;
        m_HeatHazeFXRandomShift = 0;
        m_HeatHazeFXSpeedMin    = 12;
        m_HeatHazeFXSpeedMax    = 18;
        m_HeatHazeFXScanSizeX   = uint32(SCREEN_STRETCH_X(47.0f));
        m_HeatHazeFXScanSizeY   = uint32(SCREEN_STRETCH_Y(47.0f));
        m_HeatHazeFXRenderSizeX = uint32(SCREEN_STRETCH_X(50.0f));
        m_HeatHazeFXRenderSizeY = uint32(SCREEN_STRETCH_Y(50.0f));
        break;
    case HEAT_HAZE_1:
        m_HeatHazeFXIntensity   = 32;
        m_HeatHazeFXRandomShift = 0;
        m_HeatHazeFXSpeedMin    = 6;
        m_HeatHazeFXSpeedMax    = 10;
        m_HeatHazeFXScanSizeX   = uint32(SCREEN_STRETCH_X(100.0f));
        m_HeatHazeFXScanSizeY   = uint32(SCREEN_STRETCH_Y(52.0f));
        m_HeatHazeFXRenderSizeX = uint32(SCREEN_STRETCH_X(100.0f));
        m_HeatHazeFXRenderSizeY = uint32(SCREEN_STRETCH_Y(60.0f));
        break;
    case HEAT_HAZE_2:
        m_HeatHazeFXIntensity   = 32;
        m_HeatHazeFXRandomShift = 0;
        m_HeatHazeFXSpeedMin    = 4;
        m_HeatHazeFXSpeedMax    = 8;
        m_HeatHazeFXScanSizeX   = uint32(SCREEN_STRETCH_X(70.0f));
        m_HeatHazeFXScanSizeY   = uint32(SCREEN_STRETCH_Y(70.0f));
        m_HeatHazeFXRenderSizeX = uint32(SCREEN_STRETCH_X(80.0f));
        m_HeatHazeFXRenderSizeY = uint32(SCREEN_STRETCH_Y(80.0f));
        break;
    case HEAT_HAZE_3:
        m_HeatHazeFXRandomShift = 0;
        m_HeatHazeFXIntensity   = 150;
        m_HeatHazeFXSpeedMin    = 5;
        m_HeatHazeFXSpeedMax    = 8;
        m_HeatHazeFXScanSizeX   = uint32(SCREEN_STRETCH_X(60.0f));
        m_HeatHazeFXScanSizeY   = uint32(SCREEN_STRETCH_Y(24.0f));
        m_HeatHazeFXRenderSizeX = uint32(SCREEN_STRETCH_X(62.0f));
        m_HeatHazeFXRenderSizeY = uint32(SCREEN_STRETCH_Y(24.0f));
        break;
    case HEAT_HAZE_4:
        m_HeatHazeFXRandomShift = 1;
        m_HeatHazeFXIntensity   = 150;
        m_HeatHazeFXSpeedMin    = 5;
        m_HeatHazeFXSpeedMax    = 8;
        m_HeatHazeFXScanSizeX   = uint32(SCREEN_STRETCH_X(60.0f));
        m_HeatHazeFXScanSizeY   = uint32(SCREEN_STRETCH_Y(24.0f));
        m_HeatHazeFXRenderSizeX = uint32(SCREEN_STRETCH_X(62.0f));
        m_HeatHazeFXRenderSizeY = uint32(SCREEN_STRETCH_Y(24.0f));
        break;
    default:
        break;
    }

    m_HeatHazeFXTypeLast = m_HeatHazeFXType;

    for (auto i = 0; i < 180; i++) {
        hpX[i] = CGeneral::GetRandomNumberInRange(m_HeatHazeFXScanSizeX, RwRasterGetWidth(pRasterFrontBuffer));
        hpY[i] = CGeneral::GetRandomNumberInRange(m_HeatHazeFXScanSizeY, RwRasterGetHeight(pRasterFrontBuffer));
        hpS[i] = CGeneral::GetRandomNumberInRange(m_HeatHazeFXSpeedMin,  m_HeatHazeFXSpeedMax);
    }
}

// 0x701780
void CPostEffects::HeatHazeFX(float fIntensity, bool bAlphaMaskMode) {
    plugin::Call<0x701780, float, bool>(fIntensity, bAlphaMaskMode);
}

// 0x7034F0
bool CPostEffects::IsVisionFXActive() {
    return !m_bInCutscene && (m_bNightVision || m_bInfraredVision);
}

// 0x7011C0
void CPostEffects::NightVision() {
    if (m_fNightVisionSwitchOnFXCount > 0.0f) {
        m_fNightVisionSwitchOnFXCount = std::min(m_fNightVisionSwitchOnFXCount - CTimer::GetTimeStep(), 0.0f);

        ImmediateModeRenderStatesStore();
        ImmediateModeRenderStatesSet();
        RwRenderStateSet(rwRENDERSTATESRCBLEND,  RWRSTATE(rwBLENDONE));
        RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDONE));

        for (auto i = 0, end = (int32)m_fNightVisionSwitchOnFXCount; i < end; i++) {
            DrawQuad(0.0f, 0.0f, (float)ms_imf.sizeDrawBufferX, (float)ms_imf.sizeDrawBufferY, 8, 8, 8, 255, ms_imf.RasterDrawBuffer);
        }

        ImmediateModeRenderStatesReStore();
    }

    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    RwRenderStateSet(rwRENDERSTATESRCBLEND,  RWRSTATE(rwBLENDZERO));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDSRCCOLOR));
    DrawQuad(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 32, 255, 32, 255, nullptr);
    ImmediateModeRenderStatesReStore();
}

// 0x7012E0
void CPostEffects::NightVisionSetLights() {
    if (m_bNightVision && !m_bInCutscene) {
        SetLightsForNightVision();
    }
}

// 0x703520
void CPostEffects::SetFilterMainColour(RwRaster* raster, RwRGBA color) {
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERNEAREST));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(pRasterFrontBuffer));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));

    RwIm2DVertexSetRealRGBA(&cc_vertices[0], color.red, color.green, color.blue, color.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[1], color.red, color.green, color.blue, color.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[2], color.red, color.green, color.blue, color.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[3], color.red, color.green, color.blue, color.alpha);

    RwRenderStateSet(rwRENDERSTATESRCBLEND,  RWRSTATE(rwBLENDONE));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDDESTALPHA));
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, cc_vertices, std::size(cc_vertices), cc_indices, std::size(cc_indices) / 2); // size 4 and 6

    RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(NULL));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDINVSRCALPHA));
}

// 0x703F80
void CPostEffects::InfraredVision(RwRGBA color, RwRGBA colorMain) {
    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();

    RwRenderStateSet(rwRENDERSTATESRCBLEND,  RWRSTATE(rwBLENDONE));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDONE));

    float radius = m_fInfraredVisionFilterRadius * 100.0f;
    int32 i = 0, j = 0; // todo: use for loop?
    float xy;
    do {
        j = i + 1;
        DrawQuadSetUVs(
            ms_imf.fFrontBufferU1, ms_imf.fFrontBufferV1,
            ms_imf.fFrontBufferU2, ms_imf.fFrontBufferV1,
            ms_imf.fFrontBufferU1, ms_imf.fFrontBufferV2,
            ms_imf.fFrontBufferU2, ms_imf.fFrontBufferV2
        );

        xy = (float)(j) * radius;
        DrawQuad(
            -xy,
            -xy,
            SCREEN_WIDTH + xy + xy,
            SCREEN_HEIGHT + xy + xy,
            color.red,
            color.green,
            color.blue,
            255,
            ms_imf.RasterDrawBuffer
        );
        i = j;
    } while (j < 4);

    DrawQuadSetDefaultUVs();

    RwRenderStateSet(rwRENDERSTATESRCBLEND,  RWRSTATE(rwBLENDZERO));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDSRCCOLOR));

    DrawQuad(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 64, 255, 255, nullptr);
    ImmediateModeRenderStatesReStore();
    SetFilterMainColour(ms_imf.RasterDrawBuffer, colorMain);
}

// 0x701430
void CPostEffects::InfraredVisionSetLightsForDefaultObjects() {
    if (m_bInfraredVision && !m_bInCutscene) {
        SetLightsForInfraredVisionDefaultObjects();
    }
}

// unused
// 0x701300
void CPostEffects::InfraredVisionSetLightsForHeatObjects() {
    if (m_bInfraredVision && !m_bInCutscene) {
        SetLightsForInfraredVisionHeatObjects();
    }
}

// 0x701320
void CPostEffects::InfraredVisionStoreAndSetLightsForHeatObjects(CPed* ped) {
    if (!m_bInfraredVision || m_bInCutscene)
        return;

    // store color
    const auto red   = m_fInfraredVisionHeatObjectCol.red;
    const auto green = m_fInfraredVisionHeatObjectCol.green;
    const auto blue  = m_fInfraredVisionHeatObjectCol.blue;
    const auto alpha = m_fInfraredVisionHeatObjectCol.alpha;

    // here we go to fuck cold carbon (Explanation: https://sampik.ru/articles/468-pochemu-piratskij-perevod-gtasa-takoj-strannyj.html)
    // gradually changing from red to blue (dead)
    if (ped->m_nPedState == PEDSTATE_DEAD) {
        const auto time = std::abs((int32)(CTimer::GetTimeInMS() - ped->m_nDeathTimeMS));
        const auto delta = (float)time / 10'000.0f;

        m_fInfraredVisionHeatObjectCol.red = std::max(m_fInfraredVisionHeatObjectCol.red - delta, 0.0f);
        m_fInfraredVisionHeatObjectCol.green = 0.0f;
        m_fInfraredVisionHeatObjectCol.blue = std::min(m_fInfraredVisionHeatObjectCol.blue + delta, 1.0f);
    }

    StoreAndSetLightsForInfraredVisionHeatObjects();

    // restore color
    m_fInfraredVisionHeatObjectCol.red   = red;
    m_fInfraredVisionHeatObjectCol.green = green;
    m_fInfraredVisionHeatObjectCol.blue  = blue;
    m_fInfraredVisionHeatObjectCol.alpha = alpha;
}

// 0x701410
void CPostEffects::InfraredVisionRestoreLightsForHeatObjects() {
    if (m_bInfraredVision && !m_bInCutscene) {
        RestoreLightsForInfraredVisionHeatObjects();
    }
}

// 0x704150
void CPostEffects::Fog() {
    static float& s_FogRadius = StaticRef<float>(0xC402E4);
    static float& s_FogAngle  = StaticRef<float>(0xC402DC);

    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));

    s_FogRadius = FindPlayerSpeed().SquaredMagnitude() <= sq(0.06f)
        ? std::max(s_FogRadius - CTimer::GetTimeStep() / 4.0f, 0.0f)
        : std::min(s_FogRadius + CTimer::GetTimeStep() / 4.0f, 160.0f);

    const auto skyBottom = CTimeCycle::GetCurrentSkyBottomColor();
    for (auto i = 0; i < 10; i++) {
        const auto angle = DegreesToRadians(36.0f * static_cast<float>(i) + s_FogAngle);

        DrawQuad(
            std::cos(angle) * (SCREEN_WIDTH / 4.0f + s_FogRadius) + SCREEN_WIDTH / 2.0f - SCREEN_WIDTH / 3.0f,
            std::sin(angle) * (SCREEN_HEIGHT / 4.0f + s_FogRadius) + SCREEN_HEIGHT / 2.0f - SCREEN_HEIGHT / 3.0f,
            2.0f * SCREEN_WIDTH / 3.0f,
            2.0f * SCREEN_HEIGHT / 3.0f,
            skyBottom.r,
            skyBottom.g,
            skyBottom.b,
            11,
            RwTextureGetRaster(CClouds::ms_vc.texture)
        );
    }

    for (auto i = 0; i < 10; i++) {
        const auto angle = -DegreesToRadians(36.0f * static_cast<float>(i) + s_FogAngle);

        DrawQuad(
            std::cos(angle) * (SCREEN_WIDTH * 0.35f + s_FogRadius) + SCREEN_WIDTH / 2.0f - SCREEN_WIDTH / 3.0f,
            std::sin(angle) * (SCREEN_HEIGHT * 0.35f + s_FogRadius) + SCREEN_HEIGHT / 2.0f - SCREEN_HEIGHT / 3.0f,
            2.0f * SCREEN_WIDTH / 3.0f,
            2.0f * SCREEN_HEIGHT / 3.0f,
            skyBottom.r,
            skyBottom.g,
            skyBottom.b,
            11,
            RwTextureGetRaster(CClouds::ms_vc.texture)
        );
    }

    s_FogAngle += CTimer::GetTimeStep() / 6.0f;
    ImmediateModeRenderStatesReStore();
}

// 0x702F40
void CPostEffects::CCTV() {
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, RWRSTATE(rwTEXTUREADDRESSCLAMP));
    RwCameraEndUpdate(Scene.m_pRwCamera);
    RwRasterPushContext(CPostEffects::pRasterFrontBuffer);
    RwRasterRenderFast(RwCameraGetRaster(Scene.m_pRwCamera), 0, 0);
    RwRasterPopContext();
    RsCameraBeginUpdate(Scene.m_pRwCamera);

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(true));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE, RWRSTATE(false));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(true));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE, RWRSTATE(false));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, CPostEffects::pRasterFrontBuffer);
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(true));
    RwRenderStateSet(rwRENDERSTATESRCBLEND, RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND, RWRSTATE(rwBLENDINVSRCALPHA));
    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();

    const auto lineHeight  = static_cast<uint32>(2.0f * SCREEN_STRETCH_Y(1.0f));
    const auto linePadding = 2 * lineHeight;
    const auto numLines    = static_cast<uint32>(SCREEN_HEIGHT) / linePadding;
    for (auto i = 0u, y = 0u; i < numLines; i++, y += linePadding) {
        DrawQuad(
            0.0f,
            (float)(y),
            SCREEN_WIDTH,
            static_cast<float>(lineHeight),
            m_CCTVcol.r,
            m_CCTVcol.g,
            m_CCTVcol.b,
            255,
            pRasterFrontBuffer
        );
    }
    ImmediateModeRenderStatesReStore();
}

// 0x7037C0
void CPostEffects::Grain(int32 strengthMask, bool update) {
    static uint32& s_NumberOfReseeds = StaticRef<uint32>(0xC4031C);
    if (update) {
        auto* pixels = RwRasterLock(m_pGrainRaster, 0, rwRASTERLOCKWRITE);

        // std::srand(CTimer::GetTimeInMS() + OS_TimeMS())
        std::srand(CTimer::GetCurrentTimeInCycles() / CTimer::GetCyclesPerMillisecond());
        for (auto i = 0u, reSeedCounter = 0u; i < sq(GRAIN_TEXTURE_DIM); i++) {
            if (++reSeedCounter >= 100) {
                reSeedCounter = 0;
                std::srand(CTimer::GetTimeInMS() + OS_TimeMS() + ++s_NumberOfReseeds);
            }
            pixels[4 * i] = pixels[4 * i + 1] = pixels[4 * i + 2] = pixels[4 * i + 3] = static_cast<uint8>(CGeneral::GetRandomNumber());
        }
        RwRasterUnlock(m_pGrainRaster);
    }

    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, RWRSTATE(rwTEXTUREADDRESSWRAP));
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));

    const auto ux = SCREEN_STRETCH_X(1.5f), uy = SCREEN_HEIGHT / SCREEN_WIDTH * ux;

    ms_imf.quad[0].u = 0.0f;
    ms_imf.quad[0].v = 0.0f;
    ms_imf.quad[1].u = ux;
    ms_imf.quad[1].v = 0.0f;
    ms_imf.quad[2].u = 0.0f;
    ms_imf.quad[2].v = uy;
    ms_imf.quad[3].u = ux;
    ms_imf.quad[3].v = uy;
    DrawQuad(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 255, 255, 255, strengthMask, m_pGrainRaster);
    ms_imf.quad[0].u = 0.0f;
    ms_imf.quad[0].v = 0.0f;
    ms_imf.quad[1].u = 1.0f;
    ms_imf.quad[1].v = 0.0f;
    ms_imf.quad[2].u = 0.0f;
    ms_imf.quad[2].v = 1.0f;
    ms_imf.quad[3].u = 1.0f;
    ms_imf.quad[3].v = 1.0f;
    ImmediateModeRenderStatesReStore();
}

// 0x7030A0
void CPostEffects::SpeedFX(float speed) {
    plugin::Call<0x7030A0, float>(speed);
}

// 0x702F00
void CPostEffects::DarknessFilter(int32 alpha) {
    ImmediateModeRenderStatesStore();
    ImmediateModeRenderStatesSet();
    DrawQuad(0.0f, 0.0f, SCREEN_WIDTH, SCREEN_HEIGHT, 0, 0, 0, alpha, nullptr);
    ImmediateModeRenderStatesReStore();
}

// 0x703650
void CPostEffects::ColourFilter(RwRGBA pass1, RwRGBA pass2) {
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER,     RWRSTATE(rwFILTERNEAREST));
    RwRenderStateSet(rwRENDERSTATEFOGENABLE,         RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,       RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,      RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER,     RWRSTATE(pRasterFrontBuffer));
    RwRenderStateSet(rwRENDERSTATEVERTEXALPHAENABLE, RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATESRCBLEND,          RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,         RWRSTATE(rwBLENDONE));

    RwIm2DVertexSetRealRGBA(&cc_vertices[0], pass1.red, pass1.green, pass1.blue, pass1.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[1], pass1.red, pass1.green, pass1.blue, pass1.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[2], pass1.red, pass1.green, pass1.blue, pass1.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[3], pass1.red, pass1.green, pass1.blue, pass1.alpha);
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, cc_vertices, std::size(cc_vertices), cc_indices, std::size(cc_indices) / 2); // size 4 and 6

    RwIm2DVertexSetRealRGBA(&cc_vertices[0], pass2.red, pass2.green, pass2.blue, pass2.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[1], pass2.red, pass2.green, pass2.blue, pass2.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[2], pass2.red, pass2.green, pass2.blue, pass2.alpha);
    RwIm2DVertexSetRealRGBA(&cc_vertices[3], pass2.red, pass2.green, pass2.blue, pass2.alpha);
    RwIm2DRenderIndexedPrimitive(rwPRIMTYPETRILIST, cc_vertices, std::size(cc_vertices), cc_indices, std::size(cc_indices) / 2); // size 4 and 6

    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));
    RwRenderStateSet(rwRENDERSTATEZTESTENABLE,   RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATEZWRITEENABLE,  RWRSTATE(TRUE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(NULL));
    RwRenderStateSet(rwRENDERSTATESRCBLEND,      RWRSTATE(rwBLENDSRCALPHA));
    RwRenderStateSet(rwRENDERSTATEDESTBLEND,     RWRSTATE(rwBLENDINVSRCALPHA));
}

// 0x702080
void CPostEffects::Radiosity(int32 intensityLimit, int32 filterPasses, int32 renderPasses, int32 intensity) {
    plugin::Call<0x702080>();
}

// 0x700BE0
void CPostEffects::SetSpeedFXManualSpeedCurrentFrame(float value) {
    m_fSpeedFXManualSpeedCurrentFrame = std::clamp(value, 0.0f, 1.0f);
}

// 0x7046E0
void CPostEffects::Render() {
    ZoneScoped;

    static int32& s_CurrentStrength    = StaticRef<int32>(0xC40328);
    static float& s_WaterGreen         = StaticRef<float>(0xC40324);
    static bool&  s_WaitForOneFrame    = StaticRef<bool>(0xC40321);
    static bool&  s_SavePhotoToGallery = StaticRef<bool>(0xC40320);

    if (m_bDisableAllPostEffect) {
        return;
    }

    RwRenderStateSet(rwRENDERSTATETEXTUREADDRESS, RWRSTATE(rwTEXTUREADDRESSCLAMP));
    RwCameraEndUpdate(Scene.m_pRwCamera);
    RwRasterPushContext(CPostEffects::pRasterFrontBuffer);
    RwRasterRenderFast(RwCameraGetRaster(Scene.m_pRwCamera), 0, 0);
    RwRasterPopContext();
    RsCameraBeginUpdate(Scene.m_pRwCamera);

    float& s_ExtraMult = ScopedStaticRef<float>(0xC4032C, 0xC40330, 1, 0.35f);

    if (m_bFog) {
        Fog();
    }

    auto [pass1, pass2] = CTimeCycle::GetPostFxColors();
    if (m_bNightVision || m_bInfraredVision) {
        pass1.Set(64, 64, 64);
        pass2.Set(64, 64, 64);
    }
    pass1.ScaleRGB(gfLaRiotsLightMult);

    auto colorMultFactor = 1.0f;
    if (auto* player = FindPlayerPed()) {
        const auto step   = CTimer::GetTimeStep() * SCREEN_EXTRA_MULT_CHANGE_RATE;
        auto lightFromCol = player->GetLightingFromCol(false);

        if (std::abs(lightFromCol - s_ExtraMult) >= step) {
            lightFromCol = lightFromCol <= s_ExtraMult 
                ? s_ExtraMult - step
                : s_ExtraMult + step;
        }
        s_ExtraMult = std::min(lightFromCol, SCREEN_EXTRA_MULT_BASE_CAP);
        colorMultFactor += (1.0f - s_ExtraMult / SCREEN_EXTRA_MULT_BASE_CAP) * SCREEN_EXTRA_MULT_BASE_MULT;
    }

    constexpr auto COLOR1_MULT = 1.0f, COLOR2_MULT = 1.0f;
    pass1.ScaleRGB(COLOR1_MULT * colorMultFactor);
    pass2.ScaleRGB(COLOR2_MULT * colorMultFactor);

    if (m_bColorEnable) {
        ColourFilter(pass1, pass2);
    }

    if (m_bDarknessFilter && !m_bNightVision && !m_bInfraredVision) {
        DarknessFilter(m_DarknessFilterAlpha);
        Radiosity(m_DarknessFilterRadiosityIntensityLimit, 0, 2, 255);
    }

    if (m_bSpeedFXTestMode) {
        SetSpeedFXManualSpeedCurrentFrame(1.0f);
    }

    if (m_bSpeedFX && m_bSpeedFXUserFlag && m_bSpeedFXUserFlagCurrentFrame) {
        auto* veh = FindPlayerVehicle();
        if (m_fSpeedFXManualSpeedCurrentFrame == 0.0f) {
            if (veh && !notsa::contains({ VEHICLE_TYPE_PLANE, VEHICLE_TYPE_HELI, VEHICLE_TYPE_BOAT, VEHICLE_TYPE_TRAIN }, veh->m_nVehicleType)) {
                bool fxDrawn{};
                if (veh->m_nVehicleType == VEHICLE_TYPE_AUTOMOBILE && veh->handlingFlags.bNosInst && veh->AsAutomobile()->m_fTireTemperature < 0.0f) {
                    const auto dir = veh->GetMoveSpeed().Dot(veh->GetForward());
                    if (dir > 0.2f) {
                        SetSpeedFXManualSpeedCurrentFrame(2.0f * dir * (veh->m_GasPedal + 1.0f));
                        SpeedFX(m_fSpeedFXManualSpeedCurrentFrame);
                        fxDrawn = true;
                    }
                }

                if (!fxDrawn && !CCutsceneMgr::ms_running) {
                    SpeedFX(FindPlayerSpeed().Magnitude());
                }
            }
        } else {
            SpeedFX(m_fSpeedFXManualSpeedCurrentFrame);
        }
    }
    SetSpeedFXManualSpeedCurrentFrame(0.0f);
    m_bSpeedFXUserFlagCurrentFrame = true;
    m_bInCutscene                  = CCutsceneMgr::ms_running || CCutsceneMgr::ms_cutsceneProcessing;

    if (m_bNightVision) {
        if (!m_bInCutscene) {
            NightVision();
            Grain(m_NightVisionGrainStrength, true);
        }
    } else {
        m_fNightVisionSwitchOnFXCount = m_fNightVisionSwitchOnFXTime;
    }

    if (m_bInfraredVision && !m_bInCutscene) {
        InfraredVision(m_InfraredVisionCol, m_InfraredVisionMainCol);
        Grain(m_InfraredVisionGrainStrength, true);
    }

    if (m_bRadiosity && !m_bDarknessFilter) {
        if (m_bRadiosityBypassTimeCycleIntensityLimit) {
            Radiosity(
                m_RadiosityIntensityLimit,
                m_RadiosityFilterPasses,
                m_RadiosityRenderPasses,
                m_RadiosityIntensity
            );
        } else {
            Radiosity(
                CTimeCycle::GetHighLightMinIntensity(),
                m_RadiosityFilterPasses,
                m_RadiosityRenderPasses,
                m_RadiosityIntensity
            );
        }
    }

    if (m_bRainEnable || s_CurrentStrength != 0) {
        const auto rain = 128.0f * CWeather::Rain;
        if (static_cast<float>(s_CurrentStrength) < rain) {
            s_CurrentStrength++;
        } else if (static_cast<float>(s_CurrentStrength) > rain) {
            s_CurrentStrength--;
        }
        s_CurrentStrength = std::max(s_CurrentStrength, 0);

        if (!CCullZones::CamNoRain() && !CCullZones::PlayerNoRain() && CWeather::IsUnderWater() && CGame::CanSeeOutSideFromCurrArea() && TheCamera.GetPosition().z <= 900.0f) {
            Grain(s_CurrentStrength / 4, true);
        }
    }

    if (m_bGrainEnable) {
        Grain(m_grainStrength[0], true);
    }

    if (!m_bHeatHazeFX) {
        if (CWeather::HeatHaze > 0.0f || g_fxMan.m_bHeatHazeEnabled) {
            if (CWeather::UnderWaterness < m_fWaterFXStartUnderWaterness) {
                if (CWeather::HeatHaze > 0.0f) {
                    HeatHazeFX(CWeather::HeatHazeFXControl, false);
                } else if (g_fxMan.m_bHeatHazeEnabled) {
                    HeatHazeFX(1.0f, true);
                }
            }
        } else if (CWeather::UnderWaterness >= m_fWaterFXStartUnderWaterness) {
            HeatHazeFX(1.0f, false);
        }
    }

    if (m_waterEnable || CWeather::UnderWaterness >= m_fWaterFXStartUnderWaterness) {
        CRGBA color{m_waterCol};
        color.r = std::min(color.r + 184, 255);
        color.g = static_cast<uint8>(std::min(float(color.g) + s_WaterGreen + 184.0f, 255.0f));
        color.b = std::min(color.b + 184, 255);

        const auto depthDarkness = m_bWaterDepthDarkness
            ? 1.0f - std::min(CWeather::WaterDepth, m_fWaterFullDarknessDepth) / m_fWaterFullDarknessDepth
            : 0.0f;
        color.ScaleRGB(depthDarkness);

        s_WaterGreen = std::min(s_WaterGreen + CTimer::GetTimeStep(), 24.0f);

        UnderWaterRipple(
            color,
            SCREEN_STRETCH_X(s_WaterGreen / 24.0f * m_xoffset),
            SCREEN_STRETCH_Y(m_yoffset),
            m_waterStrength,
            m_waterSpeed,
            m_waterFreq
        );
    } else {
        s_WaterGreen = 0.0f;
    }

    if (m_bCCTV) {
        CCTV();
    }

    if (CWeapon::ms_bTakePhoto && FrontEndMenuManager.m_bIsSaveDone) {
        CWeapon::ms_bTakePhoto = false;
        m_bSavePhotoFromScript = false;
    }

    if (s_WaitForOneFrame) {
        if (CWeapon::ms_bTakePhoto) {
            s_WaitForOneFrame = true;
            CTimer::Suspend();
            if (s_SavePhotoToGallery) {
                CVisibilityPlugins::RenderWeaponPedsForPC();
                CVisibilityPlugins::ms_weaponPedsForPC.Clear();
                CFileMgr::SetDirMyDocuments();

                auto photoIdx = 0;
                do {
                    notsa::format_to_sz(gString, "Gallery\\gallery{}.jpg", ++photoIdx);
                } while (fs::exists(gString));

                JPegCompressScreenToFile(TheCamera.m_pRwCamera, gString);
                CFileMgr::SetDir("");
            }
            CTimer::Resume();

            if (!FrontEndMenuManager.m_bActivateMenuNextFrame) {
                CSpecialFX::bSnapShotActive = true;
                CSpecialFX::SnapShotFrames  = false;
            }
            s_SavePhotoToGallery   = false;
            CWeapon::ms_bTakePhoto = false;
            m_bSavePhotoFromScript = false;
        }
    } else if (CWeapon::ms_bTakePhoto) {
        if (FrontEndMenuManager.m_bSavePhotos) {
            s_SavePhotoToGallery = true;
        }
        s_WaitForOneFrame = true;
    }
}
