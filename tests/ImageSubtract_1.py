import os
import math
import pdb
import unittest
import eups
import numpy
import lsst.pex.policy
import lsst.utils.tests as tests
import lsst.pex.logging as logging
import lsst.afw.image as afwImage
import lsst.afw.math as afwMath
import lsst.afw.detection as detection
import lsst.ip.diffim as ipDiff
import lsst.afw.image.testUtils as imageTest

import lsst.ip.diffim.diffimTools as ipDiffimTools

try:
    type(verbosity)
except NameError:
    verbosity = 5
logging.Trace.setVerbosity('lsst.ip.diffim', verbosity)

debugIO = 1
try:
    type(debugIO)
except NameError:
    debugIO = 0

import lsst.afw.display.ds9 as ds9
try:
    type(display)
except NameError:
    display = False

dataDir = eups.productDir("afwdata")
if not dataDir:
    raise RuntimeError("Must set up afwdata to run these tests")
imageProcDir = eups.productDir("ip_diffim")
if not imageProcDir:
    raise RuntimeError("Could not get path to ip_diffim")
policyPath = os.path.join(imageProcDir, "pipeline", "ImageSubtractStageDictionary.paf")
policy = lsst.pex.policy.Policy.createPolicy(policyPath)

InputMaskedImagePath = os.path.join(dataDir, "CFHT", "D4", "cal-53535-i-797722_small_1")
TemplateMaskedImagePath = os.path.join(dataDir, "CFHT", "D4", "cal-53535-i-797722_small_1_tmpl")

# the desired type of MaskedImage
MaskedImage = afwImage.MaskedImageF

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

def initializeTestCases():
    templateMaskedImage2 = MaskedImage(TemplateMaskedImagePath)
    templateMaskedImage  = MaskedImage(InputMaskedImagePath)
    scienceMaskedImage   = MaskedImage(InputMaskedImagePath)
    
    kernelCols = policy.get('kernelCols')
    kernelRows = policy.get('kernelRows')
    kernelSpatialOrder     = policy.get('kernelSpatialOrder')
    backgroundSpatialOrder = policy.get('backgroundSpatialOrder')

    # create basis vectors
    kernelBasisList = ipDiff.generateDeltaFunctionKernelSet(kernelCols, kernelRows)
    
    # create output kernel pointer
    kernelPtr = afwMath.LinearCombinationKernel()
    
    # and its function for spatial variation
    kernelFunction = afwMath.PolynomialFunction2D(kernelSpatialOrder)
    
    # and background function
    backgroundFunction = afwMath.PolynomialFunction2D(backgroundSpatialOrder)
    
    # make single good footprint at known object position in cal-53535-i-797722_small_1
    size = 40
    footprintList = detection.FootprintContainerT()
    footprint     = detection.Footprint( afwImage.BBox(afwImage.PointI(128 - size/2,
                                                                       128 - size/2),
                                                       size,
                                                       size) )
    
    footprintList.push_back(footprint)

    return templateMaskedImage2, templateMaskedImage, scienceMaskedImage, kernelBasisList, kernelPtr, kernelFunction, backgroundFunction, footprintList

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

class ConvolveTestCase(unittest.TestCase):
    """Test case for deriving Delta Function as best kernel to match two of the same images"""
    def setUp(self):
        testObjects = initializeTestCases()
        self.templateMaskedImage2  = testObjects[0]
        self.templateMaskedImage   = testObjects[1]
        self.scienceMaskedImage    = testObjects[2]
        self.kernelBasisList       = testObjects[3]
        self.kernelPtr             = testObjects[4]
        self.kernelFunction        = testObjects[5]
        self.backgroundFunction    = testObjects[6]
        self.footprintList         = testObjects[7]
               
    def tearDown(self):
        del self.templateMaskedImage2    
        del self.templateMaskedImage    
        del self.scienceMaskedImage    
        del self.kernelBasisList        
        del self.kernelPtr             
        del self.kernelFunction     
        del self.backgroundFunction 
        del self.footprintList         

    def testConvolve(self, sigmaX=2, sigmaY=3):
        """Make sure that you recover a known convolution kernel"""
        kernelCols = policy.get('kernelCols')
        kernelRows = policy.get('kernelRows')
        gaussFunction = afwMath.GaussianFunction2D(sigmaX,sigmaY)
        gaussKernel   = afwMath.AnalyticKernel(kernelCols, kernelRows, gaussFunction)
        convolvedScienceMaskedImage = MaskedImage( self.scienceMaskedImage.getDimensions() )
        afwMath.convolve(convolvedScienceMaskedImage,
                         self.scienceMaskedImage,
                         gaussKernel,
                         False,
                         0)

        kImageIn  = afwImage.ImageD(kernelCols, kernelRows)
        kSumIn    = gaussKernel.computeImage(kImageIn, 0.0, 0.0, False)
        if debugIO:
            kImageIn.writeFits('kiFits.fits')

        kValuesIn = numpy.zeros(kernelCols*kernelRows)

        kImageOut      = afwImage.ImageD(kernelCols, kernelRows)
        kFunctor       = ipDiff.PsfMatchingFunctorVwF(self.kernelBasisList)
            
        for footprintID, iFootprintPtr in enumerate(self.footprintList):
            footprintBBox           = iFootprintPtr.getBBox()
            imageToConvolveStamp    = MaskedImage(self.templateMaskedImage, footprintBBox)
            imageToNotConvolveStamp = MaskedImage(convolvedScienceMaskedImage, footprintBBox)

            # NOTE : need a copy() of the data, otherwise -= modifies templateMaskedImage
            #      : third argument is "deep copy"
            varEstimate             = MaskedImage(self.templateMaskedImage, footprintBBox, True)
            varEstimate            -= imageToNotConvolveStamp
            
            kFunctor.apply(imageToConvolveStamp,
                           imageToNotConvolveStamp,
                           varEstimate.getVariance(),
                           policy)

            # iterate?
            diffIm1     = ipDiff.convolveAndSubtract(imageToConvolveStamp,
                                                     imageToNotConvolveStamp,
                                                     kFunctor.getKernel(),
                                                     kFunctor.getBackground())
            kFunctor.apply(imageToConvolveStamp,
                           imageToNotConvolveStamp,
                           diffIm1.getVariance(),
                           policy)
            kernel      = kFunctor.getKernel()
            kSumOut     = kernel.computeImage(kImageOut, 0.0, 0.0, False)

            if debugIO:
                imageToConvolveStamp.writeFits('tFits_%d' % (footprintID,))
                imageToNotConvolveStamp.writeFits('sFits_%d' % (footprintID,))
                kImageOut.writeFits('koFits_%d.fits' % (footprintID,))
            if display:
                ds9.mtv(kImageIn, frame=0)
                ds9.mtv(kImageOut, frame=1)

            # Make sure it matches the input kernel
            for i in range(kImageOut.getWidth()):
                for j in range(kImageOut.getHeight()):
                    print i, j, kImageIn.get(i,j), kImageOut.get(i,j)
                    self.assertAlmostEqual(kImageIn.get(i,j), kImageOut.get(i,j), 3,
                                           "K(%d,%d): |%g - %g| = %g" %
                                           (i, j, kImageIn.get(i,j), kImageOut.get(i,j),
                                            abs(kImageIn.get(i,j)-kImageOut.get(i,j))))

            # Make sure that the background is zero
            self.assertAlmostEqual(kFunctor.getBackground(), 0.0)

            # Make sure that the kSum is scaling
            self.assertAlmostEqual(kSumIn, kSumOut)

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

class DeltaFunctionTestCase(unittest.TestCase):
    """Test case for deriving Delta Function as best kernel to match two of the same images"""
    def setUp(self):
        testObjects = initializeTestCases()
        self.templateMaskedImage2  = testObjects[0]
        self.templateMaskedImage   = testObjects[1]
        self.scienceMaskedImage    = testObjects[2]
        self.kernelBasisList       = testObjects[3]
        self.kernelPtr             = testObjects[4]
        self.kernelFunction        = testObjects[5]
        self.backgroundFunction    = testObjects[6]
        self.footprintList         = testObjects[7]
               
    def tearDown(self):
        del self.templateMaskedImage2    
        del self.templateMaskedImage    
        del self.scienceMaskedImage    
        del self.kernelBasisList        
        del self.kernelPtr             
        del self.kernelFunction     
        del self.backgroundFunction 
        del self.footprintList         

    def testDeltaFunction(self, bg=0.0, scaling=1.0):
        """Make sure that the output kernels are delta functions"""
        kernelCols = policy.get('kernelCols')
        kernelRows = policy.get('kernelRows')

        kImage   = afwImage.ImageD(kernelCols, kernelRows)
        kFunctor = ipDiff.PsfMatchingFunctorGslF(self.kernelBasisList)

        for footprintID, iFootprintPtr in enumerate(self.footprintList):
            footprintBBox            = iFootprintPtr.getBBox()
            imageToConvolveStamp     = MaskedImage(self.templateMaskedImage, footprintBBox)
            imageToNotConvolveStamp  = MaskedImage(self.scienceMaskedImage,  footprintBBox)
            
            imageToNotConvolveStamp += bg
            imageToNotConvolveStamp *= scaling

            # NOTE : need a copy() of the data, otherwise -= modifies templateMaskedImage
            #      : third argument is "deep copy"
            varEstimate             = MaskedImage(self.templateMaskedImage, footprintBBox, True)
            varEstimate            -= imageToNotConvolveStamp

            kFunctor.apply(imageToConvolveStamp,
                           imageToNotConvolveStamp,
                           varEstimate.getVariance(),
                           policy)
            kernel = kFunctor.getKernel()
            kSum   = kernel.computeImage(kImage, 0.0, 0.0, False)

            # make sure its a delta function
            for i in range(kImage.getWidth()):
                for j in range(kImage.getHeight()):
                    if i==j and i==kImage.getWidth()/2:
                        self.assertAlmostEqual(kImage.get(i,j), scaling, 3)
                    else:
                        self.assertAlmostEqual(kImage.get(i,j), 0.0, 3)

            # make sure that the background is zero
            self.assertAlmostEqual(kFunctor.getBackground(), bg, places=6)

            # make sure that the kSum is scaling
            self.assertAlmostEqual(kSum, scaling)
            
    def testBackground(self, bg=17.5):
        """Make sure that the background is correctly determined"""
        self.testDeltaFunction(bg=bg)

    def testScaling(self, scaling=1.75):
        """Make sure that the output kernel is scaled correctly"""
        self.testDeltaFunction(scaling=scaling)

#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

class DeconvolveTestCase(unittest.TestCase):
    """Make sure that the deconvolution kernel convolved with convolution kernel is delta function"""
    def setUp(self):
        testObjects = initializeTestCases()
        self.templateMaskedImage2  = testObjects[0]
        self.templateMaskedImage   = testObjects[1]
        self.scienceMaskedImage    = testObjects[2]
        self.kernelBasisList       = testObjects[3]
        self.kernelPtr             = testObjects[4]
        self.kernelFunction        = testObjects[5]
        self.backgroundFunction    = testObjects[6]
        self.footprintList         = testObjects[7]
               
    def tearDown(self):
        del self.templateMaskedImage2    
        del self.templateMaskedImage    
        del self.scienceMaskedImage    
        del self.kernelBasisList        
        del self.kernelPtr             
        del self.kernelFunction     
        del self.backgroundFunction 
        del self.footprintList         

    def testDeconvolve(self, sigma=3.0, scale=4):
        kernelCols = policy.get('kernelCols')
        kernelRows = policy.get('kernelRows')

        gaussFunction1 = afwMath.GaussianFunction2D(sigma,sigma)
        gaussKernel1   = afwMath.AnalyticKernel(gaussFunction1, scale*kernelCols+1, scale*kernelRows+1)
        gaussFunction2 = afwMath.GaussianFunction2D(sigma+2,sigma*2)
        gaussKernel2   = afwMath.AnalyticKernel(gaussFunction2, scale*kernelCols+1, scale*kernelRows+1)

        dfMaskedImage  = afwImage.MaskedImageD(2*scale*kernelCols+1, 2*scale*kernelRows+1)
        dfMaskedImage.getImage().set(scale*kernelCols+1, scale*kernelRows+1, 1)
        dfMaskedImage.getVariance().set(scale*kernelCols+1, scale*kernelRows+1, 1)
        
        maskedImage1   = afwMath.convolveNew(dfMaskedImage, gaussKernel1, 0, False)
        maskedImage2   = afwMath.convolveNew(dfMaskedImage, gaussKernel2, 0, False)

        # give it some sky so that there is no zero-valued variance
        img1  = maskedImage1.getImage()
        img1 += 1.e-4
        var1  = maskedImage1.getVariance()
        var1 += 1.e-4
        img2  = maskedImage2.getImage()
        img2 += 1.e-4
        var2  = maskedImage2.getVariance()
        var2 += 1.e-4
        # give the pixels realistic values
        maskedImage1 *= 1.e4
        maskedImage2 *= 1.e4 

        if debugIO:
            maskedImage1.writeFits('MI1a')
            maskedImage2.writeFits('MI2a')

        goodData = afwImage.BBox2i(scale*kernelCols/2+1, scale*kernelRows/2+1, scale*kernelCols, scale*kernelRows)
        maskedSubImage1 = MaskedImage(maskedImage1, goodData)
        maskedSubImage2 = MaskedImage(maskedImage2, goodData)
        kMask           = afwImage.MaskU(kernelCols, kernelRows)
        
        emptyStamp  = afwImage.MaskedImageD(maskedSubImage1.getWidth(), maskedSubImage1.getHeight())
        emptyStamp += maskedSubImage1.get()
        emptyStamp -= maskedSubImage2.get()

        if debugIO:
            maskedSubImage1.writeFits('MI1b')
            maskedSubImage2.writeFits('MI2b')

        # convolve one way
        vectorPair1 = ipDiff.computePsfMatchingKernelForFootprint2(
            maskedSubImage1.get(), maskedSubImage2.get(),
            emptyStamp, self.kernelBasisList, policy
            )
        kernelVector1, kernelErrorVector1, background1, backgroundError1 = ipDiffimTools.vectorPairToVectors(vectorPair1)
        kernel1 = afwMath.LinearCombinationKernel(self.kernelBasisList, kernelVector1)
        diffIm1 = ipDiff.convolveAndSubtract(maskedSubImage1,
                                             maskedSubImage2,
                                             kernel1,
                                             background1)
        
        kImage1 = afwImage.ImageD(kernelCols, kernelRows)
        kSum1   = kernel1.computeImage(kImage1, 0.0, 0.0, False)
        kMaskedImage1 = afwImage.MaskedImageD(kImage1, kMask)
        if debugIO:
            kImage1.writeFits('kFits1.fits')
            kMaskedImage1.writeFits('kFits1_Mi')
                                    

        # convolve the other way
        vectorPair2 = ipDiff.computePsfMatchingKernelForFootprint2(
            maskedSubImage2, maskedSubImage1,
            emptyStamp, self.kernelBasisList, policy
            )
        kernelVector2, kernelErrorVector2, background2, backgroundError2 = ipDiffimTools.vectorPairToVectors(vectorPair2)
        kernel2 = afwMath.LinearCombinationKernel(self.kernelBasisList, kernelVector2)
        diffIm2 = ipDiff.convolveAndSubtract(maskedSubImage2,
                                             maskedSubImage1,
                                             kernel2,
                                             background2)
        kImage2 = afwImage.ImageD(kernelCols, kernelRows)
        kSum2   = kernel2.computeImage(kImage2, 0.0, 0.0, False)
        kMaskedImage2 = afwImage.MaskedImageD(kImage2, kMask)
        if debugIO:
            kImage2.writeFits('kFits2.fits')
            kMaskedImage2.writeFits('kFits2_Mi')

        # check difference images
        stats1  = ipDiff.DifferenceImageStatisticsD(diffIm1)
        self.assertAlmostEqual(stats1.getResidualMean(), 0.0)
        stats2  = ipDiff.DifferenceImageStatisticsD(diffIm2)
        self.assertAlmostEqual(stats2.getResidualMean(), 0.0)
        
        if debugIO:
            diffIm1.writeFits('DI1')
            diffIm2.writeFits('DI2')
        
        # check that you get a delta function
        testConv12  = afwMath.convolveNew(kMaskedImage1, kernel2.get(), 0, False)
        testConv21  = afwMath.convolveNew(kMaskedImage2, kernel1.get(), 0, False)
        testImage12 = testConv12.getImage()
        testImage21 = testConv21.getImage()
        # normalize to sum = 1.0
        sum12 = 0.0
        sum21 = 0.0
        for i in range(testImage12.getWidth()):
            for j in range(testImage12.getHeight()):
                sum12 += testImage12.get(i,j)
                sum21 += testImage21.get(i,j)
                
        testConv12 /= sum12
        testConv21 /= sum21

        if debugIO:
            testConv12.writeFits('deltaFunc12')
            testConv21.writeFits('deltaFunc21')
        
        testImage12 = testConv12.getImage()
        testImage21 = testConv21.getImage()
        # In practice these are close but not exact due to noise
        for i in range(testImage12.getWidth()):
            for j in range(testImage12.getHeight()):
                if i==j and i==testImage12.getWidth()/2:
                    self.assertAlmostEqual(testImage12.get(i,j), 1.0, places=2)
                    self.assertAlmostEqual(testImage21.get(i,j), 1.0, places=2)
                else:
                    self.assertAlmostEqual(testImage12.get(i,j), 0.0, places=2)
                    self.assertAlmostEqual(testImage21.get(i,j), 0.0, places=2)
                
            
#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

class ConvolveAndSubtractTestCase(unittest.TestCase):
    """Make sure that the deconvolution kernel convolved with convolution kernel is delta function"""
    def setUp(self):
        testObjects = initializeTestCases()
        self.templateMaskedImage2  = testObjects[0]
        self.templateMaskedImage   = testObjects[1]
        self.scienceMaskedImage    = testObjects[2]
        self.kernelBasisList       = testObjects[3]
        self.kernelPtr             = testObjects[4]
        self.kernelFunction        = testObjects[5]
        self.backgroundFunction    = testObjects[6]
        self.footprintList         = testObjects[7]
               
    def tearDown(self):
        del self.templateMaskedImage2    
        del self.templateMaskedImage    
        del self.scienceMaskedImage    
        del self.kernelBasisList        
        del self.kernelPtr             
        del self.kernelFunction     
        del self.backgroundFunction 
        del self.footprintList         

    def testMethodCall(self):
        from lsst.pex.logging import Trace
        Trace.setVerbosity('lsst.ip.diffim', 5)
        
        kernelCols = policy.get('kernelCols')
        kernelRows = policy.get('kernelRows')
        
        footprintBBox           = self.footprintList[0].getBBox()
        imageToConvolveStamp    = MaskedImage(self.templateMaskedImage, footprintBBox)
        imageToNotConvolveStamp = MaskedImage(self.scienceMaskedImage, footprintBBox)

        
        kernel1 = afwMath.LinearCombinationKernel()
        kernel2 = afwMath.FixedKernel()
        
        self.backgroundFunction.setParameters( numpy.zeros((self.backgroundFunction.getNParameters())) )
        
        try:
            diffIm = ipDiff.convolveAndSubtract(
                #self.scienceMaskedImage,
                #self.templateMaskedImage,
                imageToConvolveStamp,
                imageToNotConvolveStamp,
                kernel1,
                self.backgroundFunction)
        except Exception, e:
            print 'LinearCombinationKernel fails'
            print e
        else:
            print 'LinearCombinationKernel works'

        try:
            diffIm = ipDiff.convolveAndSubtract(
                #self.scienceMaskedImage,
                #self.templateMaskedImage,
                imageToConvolveStamp,
                imageToNotConvolveStamp,
                kernel2,
                self.backgroundFunction)
        except:
            print 'Kernel fails'
        else:
            print 'Kernel works'
            
        
#-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-

def suite():
    """Returns a suite containing all the test cases in this module."""
    tests.init()

    suites = []
    #suites += unittest.makeSuite(ConvolveTestCase)
    suites += unittest.makeSuite(DeltaFunctionTestCase)
    #suites += unittest.makeSuite(DeconvolveTestCase)
    #
   # suites += unittest.makeSuite(ConvolveAndSubtractTestCase)
    #
    suites += unittest.makeSuite(tests.MemoryTestCase)
    return unittest.TestSuite(suites)

def run(exit=False):
    """Run the tests"""
    tests.run(suite(), exit)

if __name__ == "__main__":
    """Tests the functionality of the code"""
    run(True)
