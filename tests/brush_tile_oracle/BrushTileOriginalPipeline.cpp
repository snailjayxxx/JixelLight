#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#define BrushMaskTileRaster OriginalBrushTileRaster
#include "BrushMaskTileRasterOriginal.h"
#undef BrushMaskTileRaster
#include "BrushTileOriginalPipeline.h"
#define BrushMaskTileRaster OriginalBrushTileRaster
#define ImagePipeline BrushTileOriginalPipeline
#include "ImagePipelineOriginal.cpp"
#undef ImagePipeline
#undef BrushMaskTileRaster
