#!/usr/bin/env python
import os, sys
import eups
import time
import lsst.afw.image.imageLib as afwImage
import lsst.afw.math.mathLib as afwMath
import lsst.ip.diffim as ipDiffim
import lsst.pex.policy as pexPolicy
import lsst.pex.logging as pexLogging

import lsst.afw.display.ds9 as ds9
import lsst.afw.display.utils as displayUtils

display = True

Verbosity = 6
pexLogging.Trace_setVerbosity("lsst.ip.diffim", Verbosity)

defDataDir   = eups.productDir("afwdata") 
imageProcDir = eups.productDir("ip_diffim")

if len(sys.argv) == 1:
    defTemplatePath = os.path.join(defDataDir, "CFHT", "D4", "cal-53535-i-797722_2_tmpl")
    defSciencePath  = os.path.join(defDataDir, "CFHT", "D4", "cal-53535-i-797722_2")
elif len(sys.argv) == 3:
    defTemplatePath = sys.argv[1]
    defSciencePath  = sys.argv[2]
else:
    sys.exit(1)
    
defPolicyPath   = os.path.join(imageProcDir, "pipeline", "ImageSubtractStageDictionary.paf")
defOutputPath   = "diffImage"

templateMaskedImage = afwImage.MaskedImageF(defTemplatePath)
scienceMaskedImage  = afwImage.MaskedImageF(defSciencePath)
policy              = ipDiffim.generateDefaultPolicy(defPolicyPath)


# same for all kernels
policy.set("singleKernelClipping", True)
policy.set("kernelSumClipping", True)
policy.set("spatialKernelClipping", False)
policy.set("spatialKernelOrder", 0)
policy.set("spatialBgOrder", 0)
policy.set("usePcaForSpatialKernel", True)

footprints = ipDiffim.getCollectionOfFootprintsForPsfMatching(templateMaskedImage,
                                                              scienceMaskedImage,
                                                              policy)

# specific to delta function
policy.set("kernelBasisSet", "delta-function")
policy.set("useRegularization", False)
spatialKernel1, spatialBg1, kernelCellSet1 = ipDiffim.createPsfMatchingKernel(templateMaskedImage,
                                                                              scienceMaskedImage,
                                                                              policy,
                                                                              footprints[:20])

# alard lupton
policy.set("kernelBasisSet", "alard-lupton")
policy.set("useRegularization", False)
spatialKernel2, spatialBg2, kernelCellSet2 = ipDiffim.createPsfMatchingKernel(templateMaskedImage,
                                                                              scienceMaskedImage,
                                                                              policy,
                                                                              footprints[:20])

# regularized delta function
policy.set("kernelBasisSet", "delta-function")
policy.set("useRegularization", True)
spatialKernel3, spatialBg3, kernelCellSet3 = ipDiffim.createPsfMatchingKernel(templateMaskedImage,
                                                                              scienceMaskedImage,
                                                                              policy,
                                                                              footprints[:20])

basisList1 = spatialKernel1.getKernelList()
basisList2 = spatialKernel2.getKernelList()
basisList3 = spatialKernel3.getKernelList()

frame = 1
for idx in range(min(5, len(basisList1))):
    kernel = basisList1[idx]
    im     = afwImage.ImageD(spatialKernel1.getDimensions())
    ksum   = kernel.computeImage(im, False)    
    ds9.mtv(im, frame=frame)
    frame += 1

for idx in range(min(5, len(basisList2))):
    kernel = basisList2[idx]
    im     = afwImage.ImageD(spatialKernel2.getDimensions())
    ksum   = kernel.computeImage(im, False)    
    ds9.mtv(im, frame=frame)
    frame += 1


for idx in range(min(5, len(basisList3))):
    kernel = basisList3[idx]
    im     = afwImage.ImageD(spatialKernel3.getDimensions())
    ksum   = kernel.computeImage(im, False)    
    ds9.mtv(im, frame=frame)
    frame += 1

