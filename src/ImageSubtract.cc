// -*- lsst-c++ -*-
/**
 * @file
 *
 * @brief Implementation of image subtraction functions declared in ImageSubtract.h
 *
 * @author Andrew Becker, University of Washington
 *
 * @ingroup ip_diffim
 */
#include <iostream>
#include <limits>
#include <boost/timer.hpp> 

#include <gsl/gsl_matrix.h>
#include <gsl/gsl_vector.h>
#include <gsl/gsl_multifit.h>
#include <gsl/gsl_machine.h>
#include <gsl/gsl_blas.h>
#include <gsl/gsl_linalg.h>

// NOTE -  trace statements >= 6 can ENTIRELY kill the run time
#define LSST_MAX_TRACE 5

#include <lsst/afw/image.h>
#include <lsst/afw/math.h>
#include <lsst/pex/exceptions/Exception.h>
#include <lsst/pex/logging/Trace.h>
#include <lsst/pex/logging/Log.h>
#include <lsst/ip/diffim/ImageSubtract.h>
#include <lsst/afw/detection/Footprint.h>
#include <lsst/afw/math/ConvolveImage.h>

#define DEBUG_MATRIX 0

namespace exceptions = lsst::pex::exceptions; 
namespace logging    = lsst::pex::logging; 
namespace image      = lsst::afw::image;
namespace math       = lsst::afw::math;
namespace diffim     = lsst::ip::diffim;

//
// Constructors
//

template <typename ImageT>
diffim::DifferenceImageStatistics<ImageT>::DifferenceImageStatistics() :
    _residualMean(0.),
    _residualStd(0.)
{
}

template <typename ImageT>
diffim::DifferenceImageStatistics<ImageT>::DifferenceImageStatistics(
    const image::MaskedImage<ImageT> differenceMaskedImage
    ) :
    _residualMean(0.),
    _residualStd(0.)
{
    int nGood;
    double mean, variance;
    
    diffim::calculateMaskedImageStatistics(nGood, mean, variance, differenceMaskedImage);
    this->_residualMean = mean;
    this->_residualStd  = sqrt(variance);
}

//
// Public Member Functions
//

template <typename ImageT>
bool diffim::DifferenceImageStatistics<ImageT>::evaluateQuality(
    lsst::pex::policy::Policy &policy
    ) {
    double maxResidualMean = policy.getDouble("maximumFootprintResidualMean");
    double maxResidualStd  = policy.getDouble("maximumFootprintResidualStd");
    if ( fabs(this->getResidualMean()) > maxResidualMean ) return false;
    if ( fabs(this->getResidualStd()) > maxResidualStd ) return false;
    return true;
}

//
// Subroutines
//

/** 
 * @brief Generate a basis set of delta function Kernels.
 *
 * Generates a vector of Kernels sized nCols * nRows, where each Kernel has
 * a unique pixel set to value 1.0 with the other pixels valued 0.0.  This
 * is the "delta function" basis set.
 * 
 * @return Vector of orthonormal delta function Kernels.
 *
 * @throw lsst::pex::exceptions::DomainError if nRows or nCols not positive
 *
 * @ingroup diffim
 */
math::KernelList<math::Kernel>
diffim::generateDeltaFunctionKernelSet(
    unsigned int width, 
    unsigned int height  
    ) {
    if ((width < 1) || (height < 1)) {
        throw LSST_EXCEPT(exceptions::Exception, "nRows and nCols must be positive");
    }
    const int signedWidth = static_cast<int>(width);
    const int signedHeight = static_cast<int>(height);
    math::KernelList<math::Kernel> kernelBasisList;
    for (int row = 0; row < signedHeight; ++row) {
        for (int col = 0; col < signedWidth; ++col) {
            boost::shared_ptr<math::Kernel> 
                kernelPtr( new math::DeltaFunctionKernel(width, height, image::PointI(col,row) ) );
            kernelBasisList.push_back(kernelPtr);
        }
    }
    return kernelBasisList;
}

/** 
 * @brief Generate an Alard-Lupton basis set of Kernels.
 *
 * Not implemented.
 * 
 * @return Vector of Alard-Lupton Kernels.
 *
 * @throw lsst::pex::exceptions::DomainError if nRows or nCols not positive
 * @throw lsst::pex::exceptions::DomainError until implemented
 *
 * @ingroup diffim
 */
math::KernelList<math::Kernel>
diffim::generateAlardLuptonKernelSet(
    unsigned int nRows, 
    unsigned int nCols, 
    std::vector<double> const &sigGauss, 
    std::vector<double> const &degGauss  
    ) {
    if ((nCols < 1) || (nRows < 1)) {
        throw LSST_EXCEPT(exceptions::Exception, "nRows and nCols must be positive");
    }
    throw LSST_EXCEPT(exceptions::Exception, "Not implemented");
    
    math::KernelList<math::Kernel> kernelBasisList;
    return kernelBasisList;
}

/** 
 * @brief Implement fundamental difference imaging step of convolution and
 * subtraction : D = I - (K.x.T + bg)
 *
 * @return Difference image
 *
 * @ingroup diffim
 */
template <typename ImageT>
image::MaskedImage<ImageT> diffim::convolveAndSubtract(
    image::MaskedImage<ImageT> const &imageToConvolve,
    image::MaskedImage<ImageT> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    double background
    ) {
    
    logging::TTrace<5>("lsst.ip.diffim.convolveAndSubtract", 
                       "Convolving using convolve");
    
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    image::MaskedImage<ImageT> convolvedMaskedImage(imageToConvolve.getDimensions());
    math::convolve(convolvedMaskedImage,
                   imageToConvolve,
                   convolutionKernel,
                   false,
                   edgeMaskBit);
    
    /* Add in background */
    convolvedMaskedImage += background;
    
    /* Do actual subtraction */
    convolvedMaskedImage -= imageToNotConvolve;
    convolvedMaskedImage *= -1.0;
    
    return convolvedMaskedImage;
}

/** 
 * @brief Implement fundamental difference imaging step of convolution and
 * subtraction : D = I - (K.x.T + bg)
 *
 * @return Difference image
 *
 * @ingroup diffim
 */
template <typename ImageT>
image::MaskedImage<ImageT> diffim::convolveAndSubtract(
    image::MaskedImage<ImageT> const &imageToConvolve,
    image::MaskedImage<ImageT> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    double background
    ) {
    
    logging::TTrace<5>("lsst.ip.diffim.convolveAndSubtract", 
                       "Convolving using convolveLinear");
    
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    image::MaskedImage<ImageT> convolvedMaskedImage(imageToConvolve.getDimensions());
    math::convolveLinear(convolvedMaskedImage,
                         imageToConvolve,
                         convolutionKernel,
                         edgeMaskBit);
    
    /* Add in background */
    convolvedMaskedImage += background;
    
    /* Do actual subtraction */
    convolvedMaskedImage -= imageToNotConvolve;
    convolvedMaskedImage *= -1.0;
    
    return convolvedMaskedImage;
}

/** 
 * @brief Implement fundamental difference imaging step of convolution and
 * subtraction : D = I - (K.x.T + bg)
 *
 * @return Difference image
 *
 * @ingroup diffim
 */
template <typename ImageT, typename FunctionT>
image::MaskedImage<ImageT> diffim::convolveAndSubtract(
    image::MaskedImage<ImageT> const &imageToConvolve,
    image::MaskedImage<ImageT> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    math::Function2<FunctionT> const &backgroundFunction
    ) {
    
    logging::TTrace<5>("lsst.ip.diffim.convolveAndSubtract", 
                       "Convolving using convolve and spatially varying background");
    
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    image::MaskedImage<ImageT> convolvedMaskedImage(imageToConvolve.getDimensions());
    math::convolve(convolvedMaskedImage,
                   imageToConvolve,
                   convolutionKernel,
                   false,
                   edgeMaskBit);
    
    /* Add in background */
    addFunctionToImage(*(convolvedMaskedImage.getImage()), backgroundFunction);
    
    /* Do actual subtraction */
    convolvedMaskedImage -= imageToNotConvolve;
    convolvedMaskedImage *= -1.0;
    
    return convolvedMaskedImage;
}

/** 
 * @brief Implement fundamental difference imaging step of convolution and
 * subtraction : D = I - (K.x.T + bg)
 *
 * @return Difference image
 *
 * @ingroup diffim
 */
template <typename ImageT, typename FunctionT>
image::MaskedImage<ImageT> diffim::convolveAndSubtract(
    image::MaskedImage<ImageT> const &imageToConvolve,
    image::MaskedImage<ImageT> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    math::Function2<FunctionT> const &backgroundFunction
    ) {
    
    logging::TTrace<5>("lsst.ip.diffim.convolveAndSubtract", 
                       "Convolving using convolveLinear and spatially varying background");
    
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    image::MaskedImage<ImageT> convolvedMaskedImage(imageToConvolve.getDimensions());
    math::convolveLinear(convolvedMaskedImage,
                         imageToConvolve,
                         convolutionKernel,
                         edgeMaskBit);
    
    /* Add in background */
    addFunctionToImage(*(convolvedMaskedImage.getImage()), backgroundFunction);
    
    /* Do actual subtraction */
    convolvedMaskedImage -= imageToNotConvolve;
    convolvedMaskedImage *= -1.0;
    
    return convolvedMaskedImage;
}


/** 
 * @brief Runs Detection on a single image for significant peaks, and checks
 * returned Footprints for Masked pixels.
 *
 * Accepts two MaskedImages, one of which is to be convolved to match the
 * other.  The Detection package is run on the image to be convolved
 * (assumed to be higher S/N than the other image).  The subimages
 * associated with each returned Footprint in both images are checked for
 * Masked pixels; Footprints containing Masked pixels are rejected.  The
 * Footprints are grown by an amount specified in the Policy.  The
 * acceptible Footprints are returned in a vector.
 *
 * @return Vector of "clean" Footprints around which Image Subtraction
 * Kernels will be built.
 *
 * @ingroup diffim
 */
template <typename ImageT>
std::vector<lsst::afw::detection::Footprint> diffim::getCollectionOfFootprintsForPsfMatching(
    image::MaskedImage<ImageT> const &imageToConvolve,    
    image::MaskedImage<ImageT> const &imageToNotConvolve, 
    lsst::pex::policy::Policy &policy                                       
    ) {
    
    // Parse the Policy
    unsigned int footprintDiffimNpixMin = policy.getInt("footprintDiffimNpixMin");
    unsigned int footprintDiffimGrow    = policy.getInt("footprintDiffimGrow");
    int minimumCleanFootprints          = policy.getInt("minimumCleanFootprints");
    double footprintDetectionThreshold  = policy.getDouble("footprintDetectionThreshold");
    double detectionThresholdScaling    = policy.getDouble("detectionThresholdScaling");
    double minimumDetectionThreshold    = policy.getDouble("minimumDetectionThreshold");
    
    // Grab mask bits from the image to convolve, since that is what we'll be operating on
    int badMaskBit = imageToConvolve.getMask()->getMaskPlane("BAD");
    image::MaskPixel badPixelMask = (badMaskBit < 0) ? 0 : (1 << badMaskBit);
    
    // List of Footprints
    std::vector<lsst::afw::detection::Footprint::Ptr> footprintListIn;
    std::vector<lsst::afw::detection::Footprint> footprintListOut;

    // Functors to search through the images for bad pixels within candidate footprints
    diffim::FindSetBits<image::Mask<image::MaskPixel> > itcFunctor(imageToConvolve.getMask()); 
    diffim::FindSetBits<image::Mask<image::MaskPixel> > itncFunctor(imageToNotConvolve.getMask()); 
    
    int nCleanFootprints = 0;
    while ( (nCleanFootprints < minimumCleanFootprints) and (footprintDetectionThreshold > minimumDetectionThreshold) ) {
        footprintListIn.clear();
        footprintListOut.clear();
        
        // Find detections
        lsst::afw::detection::DetectionSet<ImageT> 
            detectionSet(imageToConvolve, lsst::afw::detection::Threshold(footprintDetectionThreshold, 
                                                                          lsst::afw::detection::Threshold::VALUE));
        
        // Get the associated footprints
        footprintListIn = detectionSet.getFootprints();
        logging::TTrace<4>("lsst.ip.diffim.getCollectionOfFootprintsForPsfMatching", 
                           "Found %d total footprints above threshold %.3f",
                           footprintListIn.size(), footprintDetectionThreshold);

        // Iterate over footprints, look for "good" ones
        nCleanFootprints = 0;
        for (std::vector<lsst::afw::detection::Footprint::Ptr>::iterator i = footprintListIn.begin(); i != footprintListIn.end(); ++i) {
            // footprint has not enough pixels 
            if (static_cast<unsigned int>((*i)->getNpix()) < footprintDiffimNpixMin) {
                continue;
            } 
            
            // Grow the footprint
            lsst::afw::detection::Footprint fpGrow = 
                lsst::afw::detection::growFootprint(*i, footprintDiffimGrow);

            // Search for bad pixels within the footprint
            itcFunctor.reset();
            itcFunctor.apply(fpGrow);
            if (itcFunctor.getBits() > 0) {
                continue;
            }
            itncFunctor.reset();
            itncFunctor.apply(fpGrow);
            if (itncFunctor.getBits() > 0) {
                continue;
            }

            // Grab a subimage; there is an exception if its e.g. too close to the image */
            image::BBox fpBBox = fpGrow.getBBox();
            try {
                lsst::afw::image::MaskedImage<ImageT> subImageToConvolve(imageToConvolve, fpBBox);
                lsst::afw::image::MaskedImage<ImageT> subImageToNotConvolve(imageToNotConvolve, fpBBox);
            } catch (exceptions::Exception& e) {
                logging::TTrace<4>("lsst.ip.diffim.getCollectionOfFootprintsForPsfMatching",
                                   "Exception caught extracting Footprint");
                logging::TTrace<5>("lsst.ip.diffim.getCollectionOfFootprintsForPsfMatching",
                                   e.what());
                continue;
            }

            // If we get this far, we have a clean footprint
            footprintListOut.push_back(fpGrow);
            nCleanFootprints += 1;
        }
        
        footprintDetectionThreshold *= detectionThresholdScaling;
    }
    logging::TTrace<3>("lsst.ip.diffim.getCollectionOfFootprintsForPsfMatching", 
                       "Found %d clean footprints above threshold %.3f",
                       footprintListOut.size(), footprintDetectionThreshold/detectionThresholdScaling);
    
    return footprintListOut;
}

/** 
 * @brief Computes a single Kernel (Model 1) around a single subimage.
 *
 * Accepts two MaskedImages, generally subimages of a larger image, one of which
 * is to be convolved to match the other.  The output Kernel is generated using
 * an input list of basis Kernels by finding the coefficients in front of each
 * basis.  This version accepts an input variance image, and uses GSL for the
 * matrices.
 *
 * @return Vector of coefficients representing the relative contribution of
 * each input basis function.
 *
 * @return Differential background offset between the two images
 *
 * @ingroup diffim
 */
template <typename ImageT>
void diffim::computePsfMatchingKernelForFootprint(
    image::MaskedImage<ImageT>         const &imageToConvolve,    
    image::MaskedImage<ImageT>         const &imageToNotConvolve, 
    image::MaskedImage<ImageT>         const &varianceImage,      
    math::KernelList<math::Kernel>            const &kernelInBasisList,  
    lsst::pex::policy::Policy       &policy,
    boost::shared_ptr<math::Kernel> &kernelPtr,
    boost::shared_ptr<math::Kernel> &kernelErrorPtr,
    double                          &background,
    double                          &backgroundError
    ) { 

    typedef typename image::MaskedImage<ImageT>::xy_locator xy_locator;

    // grab mask bits from the image to convolve, since that is what we'll be operating on
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    
    int nKernelParameters = 0;
    int nBackgroundParameters = 0;
    int nParameters = 0;
    
    boost::timer t;
    double time;
    t.restart();
    
    nKernelParameters     = kernelInBasisList.size();
    nBackgroundParameters = 1;
    nParameters           = nKernelParameters + nBackgroundParameters;
    
    gsl_vector *B = gsl_vector_alloc (nParameters);
    gsl_matrix *M = gsl_matrix_alloc (nParameters, nParameters);
    
    gsl_vector_set_zero(B);
    gsl_matrix_set_zero(M);
    
    std::vector<boost::shared_ptr<image::MaskedImage<ImageT> > > convolvedImageList(nKernelParameters);
    typename std::vector<boost::shared_ptr<image::MaskedImage<ImageT> > >::iterator 
        citer = convolvedImageList.begin();
    std::vector<boost::shared_ptr<math::Kernel> >::const_iterator 
        kiter = kernelInBasisList.begin();
    
    // Create C_ij in the formalism of Alard & Lupton */
    for (; kiter != kernelInBasisList.end(); ++kiter, ++citer) {
        
        /* NOTE : we could also *precompute* the entire template image convolved with these functions */
        /*        and save them somewhere to avoid this step each time.  however, our paradigm is to */
        /*        compute whatever is needed on the fly.  hence this step here. */
        image::MaskedImage<ImageT> image(imageToConvolve.getDimensions());
        math::convolve(image,
                       imageToConvolve,
                       **kiter,
                       false,
                       edgeMaskBit);
        boost::shared_ptr<image::MaskedImage<ImageT> > imagePtr(image);
        *citer = imagePtr;
    } 
    
    kiter = kernelInBasisList.begin();
    citer = convolvedImageList.begin();

    // Ignore buffers around edge of convolved images :
    //
    // If the kernel has width 5, it has center pixel 2.  The first good pixel
    // is the (5-2)=3rd pixel, which is array index 2, and ends up being the
    // index of the central pixel.
    //
    // You also have a buffer of unusable pixels on the other side, numbered
    // width-center-1.  The last good usable pixel is N-width+center+1.

    // Example : the kernel is width = 5, center = 2
    //
    //     ---|---|-c-|---|---|
    //          
    //           the image is width = N
    //           convolve this with the kernel, and you get
    //
    //    |-x-|-x-|-g-|---|---| ... |---|---|-g-|-x-|-x-|
    //
    //           g = first/last good pixel
    //           x = bad
    // 
    //           the first good pixel is the array index that has the value "center", 2
    //           the last good pixel has array index N-(5-2)+1
    //           eg. if N = 100, you want to use up to index 97
    //               100-3+1 = 98, and the loops use i < 98, meaning the last
    //               index you address is 97.
   
    unsigned int startCol = (*kiter)->getCtrX();
    unsigned int startRow = (*kiter)->getCtrY();
    unsigned int endCol   = (*citer)->getWidth()  - ((*kiter)->getWidth()  - (*kiter)->getCtrX()) + 1;
    unsigned int endRow   = (*citer)->getHeight() - ((*kiter)->getHeight() - (*kiter)->getCtrY()) + 1;

    std::vector<xy_locator> convolvedLocatorList;
    for (citer = convolvedImageList.begin(); citer != convolvedImageList.end(); ++citer) {
        convolvedLocatorList.push_back( **citer.xy_at(startCol,startRow) );
    }
    xy_locator imageToConvolveLocator    = imageToConvolve.xy_at(startCol, startRow);
    xy_locator imageToNotConvolveLocator = imageToNotConvolve.xy_at(startCol, startRow);
    xy_locator varianceLocator           = varianceImage.xy_at(startCol, startRow);

    for (unsigned int row = startRow; row < endRow; ++row) {
        
        for (unsigned int col = startCol; col < endCol; ++col) {
            
            ImageT ncImage          = imageToNotConvolveLocator.image();
            ImageT ncVariance       = imageToNotConvolveLocator.variance();
            image::MaskPixel ncMask = imageToNotConvolveLocator.mask();
            ImageT iVariance        = 1.0 / varianceLocator.variance();
            
            logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                               "Accessing image row %d col %d : %.3f %.3f %d",
                               row, col, ncImage, ncVariance, ncMask);
            
            // kernel index i
            typename std::vector<xy_locator>::iterator 
                kiteri = convolvedLocatorList.begin();
            typename std::vector<xy_locator>::iterator 
                kiterE = convolvedLocatorList.end();

            for (int kidxi = 0; kiteri != kiterE; ++kiteri, ++kidxi) {
                ImageT cdImagei = (*kiteri)->image();
                
                // kernel index j
                typename std::vector<xy_locator>::iterator 
                    kiterj = kiteri;

                for (int kidxj = kidxi; kiterj != kiterE; ++kiterj, ++kidxj) {
                    ImageT cdImagej = (*kiterj)->image();
                    
                    *gsl_matrix_ptr(M, kidxi, kidxj) += cdImagei * cdImagej * iVariance;
                } 
                
                *gsl_vector_ptr(B, kidxi) += ncImage * cdImagei * iVariance;
                
                // Constant background term; effectively j=kidxj+1 */
                *gsl_matrix_ptr(M, kidxi, nParameters-1) += cdImagei * iVariance;
            } 
            
            // Background term; effectively i=kidxi+1 
            *gsl_vector_ptr(B, nParameters-1)                += ncImage * iVariance;
            *gsl_matrix_ptr(M, nParameters-1, nParameters-1) += 1.0 * iVariance;
            
            logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                               "Background terms : %.3f %.3f",
                               gsl_vector_get(B, nParameters-1), 
                               gsl_matrix_get(M, nParameters-1, nParameters-1));
            
            // Step each accessor in column
            ++imageToConvolveLocator.x();
            ++imageToNotConvolveLocator.x();
            ++varianceLocator.x();
            for (int ki = 0; ki < nKernelParameters; ++ki) {
                ++convolvedLocatorList[ki].x();
            }             
            
        } // col
        
        // Step each accessor in row
        ++imageToConvolveLocator.y();
        ++imageToNotConvolveLocator.y();
        ++varianceLocator.y();
        for (int ki = 0; ki < nKernelParameters; ++ki) {
            ++convolvedLocatorList[ki].y();
        }
        
    } // row

    
    /** @note If we are going to regularize the solution to M, this is the place
     * to do it 
     */
    
    // Fill in rest of M
    for (int kidxi=0; kidxi < nParameters; ++kidxi) 
        for (int kidxj=kidxi+1; kidxj < nParameters; ++kidxj) 
            gsl_matrix_set(M, kidxj, kidxi,
                           gsl_matrix_get(M, kidxi, kidxj));
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time before matrix inversions : %.2f s", time);
    
    gsl_multifit_linear_workspace *work = gsl_multifit_linear_alloc (nParameters, nParameters);
    gsl_vector *X                       = gsl_vector_alloc (nParameters);
    gsl_matrix *Cov                     = gsl_matrix_alloc (nParameters, nParameters);
    //double chi2;
    size_t rank;
    /* This has too much other computation in there; take only what I need 
       gsl_multifit_linear_svd(M, B, GSL_DBL_EPSILON, &rank, X, Cov, &chi2, work);
    */
    gsl_matrix *A   = work->A;
    gsl_matrix *Q   = work->Q;
    gsl_matrix *QSI = work->QSI;
    gsl_vector *S   = work->S;
    gsl_vector *xt  = work->xt;
    gsl_vector *D   = work->D;
    gsl_matrix_memcpy (A, M);
    gsl_linalg_balance_columns (A, D);
    gsl_linalg_SV_decomp_mod (A, QSI, Q, S, xt);
    gsl_blas_dgemv (CblasTrans, 1.0, A, B, 0.0, xt);
    gsl_matrix_memcpy (QSI, Q);
    {
        double alpha0 = gsl_vector_get (S, 0);
        size_t p_eff = 0;
        
        const size_t p = M->size2;
        
        for (size_t j = 0; j < p; j++)
            {
                gsl_vector_view column = gsl_matrix_column (QSI, j);
                double alpha = gsl_vector_get (S, j);
                
                if (alpha <= GSL_DBL_EPSILON * alpha0) {
                    alpha = 0.0;
                } else {
                    alpha = 1.0 / alpha;
                    p_eff++;
                }
                
                gsl_vector_scale (&column.vector, alpha);
            }
        
        rank = p_eff;
    }
    gsl_vector_set_zero (X);
    gsl_blas_dgemv (CblasNoTrans, 1.0, QSI, xt, 0.0, X);
    gsl_vector_div (X, D);
    gsl_multifit_linear_free(work);
    
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time after matrix inversions : %.2f s", time);
    
    // Translate from GSL vectors into LSST classes
    unsigned int kCols = policy.getInt("kernelCols");
    unsigned int kRows = policy.getInt("kernelRows");
    std::vector<double> kValues(kCols*kRows);
    std::vector<double> kErrValues(kCols*kRows);
    for (unsigned int row = 0, idx = 0; row < kRows; row++) {
        for (unsigned int col = 0; col < kCols; col++, idx++) {
            
            // Insanity checking
            if (std::isnan( gsl_vector_get(X,idx) )) {
                throw LSST_EXCEPT(exceptions::Exception, "Unable to determine kernel solution (nan)");
            }
            if (std::isnan( gsl_matrix_get(Cov, idx, idx) )) {
                throw LSST_EXCEPT(exceptions::Exception, "Unable to determine kernel uncertainty (nan)");
            }
            if (gsl_matrix_get(Cov, idx, idx) < 0.0) {
                throw LSST_EXCEPT(exceptions::Exception,
                                  boost::format("Unable to determine kernel uncertainty, negative variance (%.3e)") % 
                                  gsl_matrix_get(Cov, idx, idx)
                                  );
            }
            
            kValues[idx]    = gsl_vector_get(X, idx);
            kErrValues[idx] = sqrt(gsl_matrix_get(Cov, idx, idx));
        }
    }
    kernelPtr = boost::shared_ptr<math::Kernel> 
        (new math::LinearCombinationKernel(kernelInBasisList, kValues));
    kernelErrorPtr = boost::shared_ptr<math::Kernel> 
        (new math::LinearCombinationKernel(kernelInBasisList, kErrValues));
    
    // Estimate of Background and Background Error */
    if (std::isnan( gsl_matrix_get(Cov, nParameters-1, nParameters-1) )) {
        throw LSST_EXCEPT(exceptions::Exception, "Unable to determine kernel uncertainty (nan)");
    }
    if (gsl_matrix_get(Cov, nParameters-1, nParameters-1) < 0.0) {
        throw LSST_EXCEPT(exceptions::Exception, 
                          boost::format("Unable to determine kernel uncertainty, negative variance (%.3e)") % 
                          gsl_matrix_get(Cov, nParameters-1, nParameters-1) 
                          );
    }
    background      = gsl_vector_get(X, nParameters-1);
    backgroundError = sqrt(gsl_matrix_get(Cov, nParameters-1, nParameters-1));
    
}

/** 
 * @brief Calculates mean and unbiased variance of values (normalized by the
 * sqrt of the image variance) in a MaskedImage
 *
 * The pixel values in the image portion are normalized by the sqrt of the
 * variance.  The mean and variance of this distribution are calculated.  If
 * the MaskedImage is a difference image, the results should follow a
 * normal(0,1) distribution.
 *
 * @return Number of unmasked pixels in the image, and the mean and variance
 * of the residuals divided by the sqrt(variance).
 *
 * @ingroup diffim
 */
template <typename ImageT, typename MaskT>
void diffim::calculateMaskedImageStatistics(
    int &nGoodPixels, 
    double &mean, 
    double &variance, 
    image::MaskedImage<ImageT> const &inputImage, 
    MaskT const badPixelMask
    ) {
    
    double x2Sum=0.0, xSum=0.0;
    nGoodPixels = 0;

    // Set the pixels row by row, to avoid repeated checks for end-of-row
    for (int y = 0; y != inputImage.getHeight(); ++y) {
        for (typename image::MaskedImage<ImageT>::x_iterator ptr = inputImage.row_begin(y); 
             ptr != inputImage.row_end(y); ++ptr) {
            if ((ptr.mask() & badPixelMask) == 0) {
                xSum        += ptr.image() / sqrt(ptr.variance());
                x2Sum       += ptr.image() * ptr.image() / ptr.variance();
                nGoodPixels += 1;
            }
        }
    }
    
    if (nGoodPixels > 0) {
        mean = xSum / nGoodPixels;
    } else {
        mean = std::numeric_limits<double>::quiet_NaN();
    }
    if (nGoodPixels > 1) {
        variance  = x2Sum / nGoodPixels - mean*mean;
        variance *= nGoodPixels / (nGoodPixels - 1);
    } else {
        variance = std::numeric_limits<double>::quiet_NaN();
    }
    
}

/** 
 * @brief Calculates mean and unbiased variance of values (normalized by the
 * sqrt of the image variance) in a MaskedImage.  This version does not look for
 * particular mask bits, instead requiring Mask == 0.
 *
 * @return Number of unmasked pixels in the image, and the mean and variance
 * of the residuals divided by the sqrt(variance).
 *
 * @ingroup diffim
 */
template <typename ImageT>
void diffim::calculateMaskedImageStatistics(
    int &nGoodPixels, 
    double &mean, 
    double &variance, 
    image::MaskedImage<ImageT> const &inputImage
    ) {

    
    double x2Sum=0.0, xSum=0.0;
    nGoodPixels = 0;

    // Set the pixels row by row, to avoid repeated checks for end-of-row
    for (int y = 0; y != inputImage.getHeight(); ++y) {
        for (typename image::MaskedImage<ImageT>::x_iterator ptr = inputImage.row_begin(y); 
             ptr != inputImage.row_end(y); ++ptr) {
            if (ptr.mask() == 0) {
                xSum        += ptr.image() / sqrt(ptr.variance());
                x2Sum       += ptr.image() * ptr.image() / ptr.variance();
                nGoodPixels += 1;
            }
        }
    }
    
    if (nGoodPixels > 0) {
        mean = xSum / nGoodPixels;
    } else {
        mean = std::numeric_limits<double>::quiet_NaN();
    }
    if (nGoodPixels > 1) {
        variance  = x2Sum / nGoodPixels - mean*mean;
        variance *= nGoodPixels / (nGoodPixels - 1);
    } else {
        variance = std::numeric_limits<double>::quiet_NaN();
    }
}

/** 
 * @brief Adds a Function to an Image
 *
 * @ingroup diffim
 */
template <typename PixelT, typename FunctionT>
void diffim::addFunctionToImage(
    image::Image<PixelT> &image,
    math::Function2<FunctionT> const &function
    ) {

    // Set the pixels row by row, to avoid repeated checks for end-of-row
    for (int y = 0; y != image.getHeight(); ++y) {
        for (typename image::Image<ImageT>::x_iterator ptr = image.row_begin(y); 
             ptr != image.row_end(y); ++ptr) {

        }
    }

    typedef typename image::Image<PixelT>::pixel_accessor imageAccessorType;
    unsigned int numCols = image.getCols();
    unsigned int numRows = image.getRows();
    imageAccessorType imRow = image.origin();
    for (unsigned int row = 0; row < numRows; ++row, imRow.next_row()) {
        imageAccessorType imCol = imRow;
        double rowPos = image::positionToIndex(row);
        for (unsigned int col = 0; col < numCols; ++col, imCol.next_col()) {
            double colPos = image::positionToIndex(col);
            *imCol += static_cast<PixelT>(function(colPos, rowPos));
        }
    }
}

// Explicit instantiations

template class diffim::DifferenceImageStatistics<float>;
template class diffim::DifferenceImageStatistics<double>;

/* */

template 
image::MaskedImage<float> diffim::convolveAndSubtract(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    double background);

template 
image::MaskedImage<double> diffim::convolveAndSubtract(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    double background);

template 
image::MaskedImage<float> diffim::convolveAndSubtract(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    double background);

template 
image::MaskedImage<double> diffim::convolveAndSubtract(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    double background);

/* */

template 
image::MaskedImage<float> diffim::convolveAndSubtract(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    math::Function2<double> const &backgroundFunction);

template 
image::MaskedImage<double> diffim::convolveAndSubtract(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    math::Kernel const &convolutionKernel,
    math::Function2<double> const &backgroundFunction);


template 
image::MaskedImage<float> diffim::convolveAndSubtract(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    math::Function2<double> const &backgroundFunction);


template 
image::MaskedImage<double> diffim::convolveAndSubtract(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    math::LinearCombinationKernel const &convolutionKernel,
    math::Function2<double> const &backgroundFunction);


/* */

template
void diffim::computePsfMatchingKernelForFootprint(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    image::MaskedImage<float> const &varianceImage,
    math::KernelList<math::Kernel> const &kernelInBasisList,
    lsst::pex::policy::Policy       &policy,
    boost::shared_ptr<math::Kernel> &kernelPtr,
    boost::shared_ptr<math::Kernel> &kernelErrorPtr,
    double                          &background,
    double                          &backgroundError);

template
void diffim::computePsfMatchingKernelForFootprint(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    image::MaskedImage<double> const &varianceImage,
    math::KernelList<math::Kernel> const &kernelInBasisList,
    lsst::pex::policy::Policy       &policy,
    boost::shared_ptr<math::Kernel> &kernelPtr,
    boost::shared_ptr<math::Kernel> &kernelErrorPtr,
    double                          &background,
    double                          &backgroundError);

template
std::vector<lsst::afw::detection::Footprint> diffim::getCollectionOfFootprintsForPsfMatching(
    image::MaskedImage<float> const &imageToConvolve,
    image::MaskedImage<float> const &imageToNotConvolve,
    lsst::pex::policy::Policy &policy);

template
std::vector<lsst::afw::detection::Footprint> diffim::getCollectionOfFootprintsForPsfMatching(
    image::MaskedImage<double> const &imageToConvolve,
    image::MaskedImage<double> const &imageToNotConvolve,
    lsst::pex::policy::Policy &policy);

template
void diffim::calculateMaskedImageStatistics(
    int &nGoodPixels,
    double &mean,
    double &variance,
    image::MaskedImage<float> const &inputImage,
    image::MaskPixel const badPixelMask);

template
void diffim::calculateMaskedImageStatistics(
    int &nGoodPixels,
    double &mean,
    double &variance,
    image::MaskedImage<double> const &inputImage,
    image::MaskPixel const badPixelMask);

template
void diffim::addFunctionToImage(
    image::Image<float>&,
    math::Function2<float> const&);

template
void diffim::addFunctionToImage(
    image::Image<float>&,
    math::Function2<double> const&);

template
void diffim::addFunctionToImage(
    image::Image<double>&,
    math::Function2<float> const&);

template
void diffim::addFunctionToImage(
    image::Image<double>&,
    math::Function2<double> const&);



#if false
/** 
 * @brief Computes a single Kernel (Model 1) around a single subimage.
 *
 * Accepts two MaskedImages, generally subimages of a larger image, one of which
 * is to be convolved to match the other.  The output Kernel is generated using
 * an input list of basis Kernels by finding the coefficients in front of each
 * basis.  This version accepts an input variance image.
 *
 * @return Vector of coefficients representing the relative contribution of
 * each input basis function.
 *
 * @return Differential background offset between the two images
 *
 * @ingroup diffim
 */
template <typename ImageT, typename MaskT>
void diffim::computePsfMatchingKernelForFootprint(
    image::MaskedImage<ImageT, MaskT>         const &imageToConvolve,    
    image::MaskedImage<ImageT, MaskT>         const &imageToNotConvolve, 
    image::MaskedImage<ImageT, MaskT>         const &varianceImage,      
    math::KernelList<math::Kernel> const &kernelInBasisList,  
    lsst::pex::policy::Policy                  &policy,
    boost::shared_ptr<math::Kernel> &kernelPtr,
    boost::shared_ptr<math::Kernel> &kernelErrorPtr,
    double                                     &background,
    double                                     &backgroundError
    ) { 
    
    // grab mask bits from the image to convolve, since that is what we'll be operating on
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    
    int nKernelParameters = 0;
    int nBackgroundParameters = 0;
    int nParameters = 0;
    
    boost::timer t;
    double time;
    t.restart();
    
    nKernelParameters     = kernelInBasisList.size();
    nBackgroundParameters = 1;
    nParameters           = nKernelParameters + nBackgroundParameters;
    
    vw::math::Vector<double> B(nParameters);
    vw::math::Matrix<double> M(nParameters, nParameters);
    for (unsigned int i = nParameters; i--;) {
        B(i) = 0;
        for (unsigned int j = nParameters; j--;) {
            M(i,j) = 0;
        }
    }
    
    std::vector<boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > > convolvedImageList(nKernelParameters);
    typename std::vector<boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > >::iterator 
        citer = convolvedImageList.begin();
    std::vector<boost::shared_ptr<math::Kernel> >::const_iterator 
        kiter = kernelInBasisList.begin();
    
    // Create C_ij in the formalism of Alard & Lupton
    for (; kiter != kernelInBasisList.end(); ++kiter, ++citer) {
        
        /* NOTE : we could also *precompute* the entire template image convolved with these functions */
        /*        and save them somewhere to avoid this step each time.  however, our paradigm is to */
        /*        compute whatever is needed on the fly.  hence this step here. */
        boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > imagePtr(
                                                                       new image::MaskedImage<ImageT, MaskT>
                                                                       (math::convolveNew(imageToConvolve, **kiter, edgeMaskBit, false))
                                                                       );
        
        *citer = imagePtr;
    } 
    
    kiter = kernelInBasisList.begin();
    citer = convolvedImageList.begin();
    unsigned int startCol = (*kiter)->getCtrCol();
    unsigned int startRow = (*kiter)->getCtrRow();
    
    unsigned int endCol   = (*citer)->getCols() - ((*kiter)->getCols() - (*kiter)->getCtrCol()) + 1;
    unsigned int endRow   = (*citer)->getRows() - ((*kiter)->getRows() - (*kiter)->getCtrRow()) + 1;
    
    std::vector<image::MaskedPixelAccessor<ImageT, MaskT> > convolvedAccessorRowList;
    for (citer = convolvedImageList.begin(); citer != convolvedImageList.end(); ++citer) {
        convolvedAccessorRowList.push_back(image::MaskedPixelAccessor<ImageT, MaskT>(**citer));
    }
    
    // An accessor for each input image; address rows and cols separately
    image::MaskedPixelAccessor<ImageT, MaskT> imageToConvolveRow(imageToConvolve);
    image::MaskedPixelAccessor<ImageT, MaskT> imageToNotConvolveRow(imageToNotConvolve);
    image::MaskedPixelAccessor<ImageT, MaskT> varianceRow(varianceImage);
    
    // Address input images
    imageToConvolveRow.advance(startCol, startRow);
    imageToNotConvolveRow.advance(startCol, startRow);
    varianceRow.advance(startCol, startRow);
    // Address kernel images
    for (int ki = 0; ki < nKernelParameters; ++ki) {
        convolvedAccessorRowList[ki].advance(startCol, startRow);
    }
    
    for (unsigned int row = startRow; row < endRow; ++row) {
        // An accessor for each convolution plane 
        std::vector<image::MaskedPixelAccessor<ImageT, MaskT> > convolvedAccessorColList = convolvedAccessorRowList;
        
        // An accessor for each input image
        image::MaskedPixelAccessor<ImageT, MaskT> imageToConvolveCol = imageToConvolveRow;
        image::MaskedPixelAccessor<ImageT, MaskT> imageToNotConvolveCol = imageToNotConvolveRow;
        image::MaskedPixelAccessor<ImageT, MaskT> varianceCol = varianceRow;
        
        for (unsigned int col = startCol; col < endCol; ++col) {
            
            ImageT ncImage   = *imageToNotConvolveCol.image;
            ImageT ncVariance = *imageToNotConvolveCol.variance;
            MaskT  ncMask     = *imageToNotConvolveCol.mask;
            
            ImageT iVariance  = 1.0 / *varianceCol.variance;
            
            logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                               "Accessing image row %d col %d : %.3f %.3f %d",
                               row, col, ncImage, ncVariance, ncMask);
            
            // kernel index i
            typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator
                kiteri   = convolvedAccessorColList.begin();
            typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator
                kiterEnd = convolvedAccessorColList.end();
            
            for (int kidxi = 0; kiteri != kiterEnd; ++kiteri, ++kidxi) {
                ImageT cdImagei   = *kiteri->image;
                
                /** @note Commenting in these additional pixel accesses yields
                 * an additional second of run-time per kernel with opt=1 at 2.8
                 * GHz
                 */
                
                /* ignore unnecessary pixel accesses 
                   ImageT cdVariancei = *kiteri->variance;
                   MaskT  cdMaski     = *kiteri->mask;
                   logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                   "Accessing convolved image %d : %.3f %.3f %d",
                   kidxi, cdImagei, cdVariancei, cdMaski);
                */
                
                // kernel index j
                typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator kiterj = kiteri;
                
                for (int kidxj = kidxi; kiterj != kiterEnd; ++kiterj, ++kidxj) {
                    ImageT cdImagej   = *kiterj->image;
                    M[kidxi][kidxj]   += cdImagei * cdImagej * iVariance;
                } 
                
                B[kidxi] += ncImage * cdImagei * iVariance;
                
                // Constant background term; effectively j=kidxj+1 */
                M[kidxi][nParameters-1] += cdImagei * iVariance;
            } 
            
            // Background term; effectively i=kidxi+1 */
            B[nParameters-1]                += ncImage * iVariance;
            M[nParameters-1][nParameters-1] += 1.0 * iVariance;
            
            logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                               "Background terms : %.3f %.3f",
                               B[nParameters-1], M[nParameters-1][nParameters-1]);
            
            // Step each accessor in column 
            imageToConvolveCol.nextCol();
            imageToNotConvolveCol.nextCol();
            varianceCol.nextCol();
            for (int ki = 0; ki < nKernelParameters; ++ki) {
                convolvedAccessorColList[ki].nextCol();
            }             
            
        } // col
        
        // Step each accessor in row
        imageToConvolveRow.nextRow();
        imageToNotConvolveRow.nextRow();
        varianceRow.nextRow();
        for (int ki = 0; ki < nKernelParameters; ++ki) {
            convolvedAccessorRowList[ki].nextRow();
        }
        
    } // row
    
    /** @note If we are going to regularize the solution to M, this is the place
     * to do it 
     */
    
    // Fill in rest of M
    for (int kidxi=0; kidxi < nParameters; ++kidxi) 
        for (int kidxj=kidxi+1; kidxj < nParameters; ++kidxj) 
            M[kidxj][kidxi] = M[kidxi][kidxj];
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time before matrix inversions : %.2f s", time);
    
    /* Invert using SVD and Pseudoinverse : This is a full second slower per
     * kernel than vw::math::least_squares, compiled at opt=1 at 2.8 GHz.
     
     vw::math::Matrix<double> Minv = vw::math::pseudoinverse(M);
     vw::math::Vector<double> Soln = Minv * B;
    */     
    
    // Invert using VW's internal method
    vw::math::Vector<double> kSolution = vw::math::least_squares(M, B);
    
    // Additional gymnastics to get the parameter uncertainties
    vw::math::Matrix<double> Mt        = vw::math::transpose(M);
    vw::math::Matrix<double> MtM       = Mt * M;
    vw::math::Matrix<double> kError    = vw::math::pseudoinverse(MtM);
    
    /*
      NOTE : for any real kernels I have looked at, these solutions have agreed
      exactly.  However, when designing the testDeconvolve unit test with
      hand-built gaussians as objects and non-realistic noise, the solutions did
      *NOT* agree.
      
      std::cout << "Soln : " << Soln << std::endl;
      std::cout << "Soln2 : " << Soln2 << std::endl;
    */
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time after matrix inversions : %.2f s", time);
    
    // Translate from VW vectors into LSST classes
    unsigned int kCols = policy.getInt("kernelCols");
    unsigned int kRows = policy.getInt("kernelRows");
    vector<double> kValues(kCols*kRows);
    vector<double> kErrValues(kCols*kRows);
    for (unsigned int row = 0, idx = 0; row < kRows; row++) {
        for (unsigned int col = 0; col < kCols; col++, idx++) {
            
            // Insanity checking
            if (std::isnan(kSolution[idx])) {
                throw LSST_EXCEPT(exceptions::Exception, "Unable to determine kernel solution (nan)");
            }
            if (std::isnan(kError[idx][idx])) {
                throw LSST_EXCEPT(exceptions::Exception, "Unable to determine kernel uncertainty (nan)");
            }
            if (kError[idx][idx] < 0.0) {
                throw LSST_EXCEPT(exceptions::Exception
                                  boost::format("Unable to determine kernel uncertainty, negative variance (%.3e)") % 
                                  kError[idx][idx]
                                  );
            }
            
            kValues[idx]    = kSolution[idx];
            kErrValues[idx] = sqrt(kError[idx][idx]);
        }
    }
    kernelPtr = boost::shared_ptr<math::Kernel> (
                                                 new math::LinearCombinationKernel(kernelInBasisList, kValues)
                                                 );
    kernelErrorPtr = boost::shared_ptr<math::Kernel> (
                                                      new math::LinearCombinationKernel(kernelInBasisList, kErrValues)
                                                      );
    
    // Estimate of Background and Background Error */
    if (std::isnan(kError[nParameters-1][nParameters-1])) {
        throw LSST_EXCEPT(exceptions::Exception, "Unable to determine background uncertainty (nan)");
    }
    if (kError[nParameters-1][nParameters-1] < 0.0) {
        throw LSST_EXCEPT(exceptions::Exception,
                          boost::format("Unable to determine background uncertainty, negative variance (%.3e)") % 
                          kError[nParameters-1][nParameters-1]
                          );
    }
    background      = kSolution[nParameters-1];
    backgroundError = sqrt(kError[nParameters-1][nParameters-1]);
}


/** 
 * @brief Computes a single Kernel (Model 1) around a single subimage.
 *
 * Accepts two MaskedImages, generally subimages of a larger image, one of which
 * is to be convolved to match the other.  The output Kernel is generated using
 * an input list of basis Kernels by finding the coefficients in front of each
 * basis.  An old version with comments and considerations for posterity.
 *
 * @return Vector of coefficients representing the relative contribution of
 * each input basis function.
 *
 * @return Differential background offset between the two images
 *
 * @ingroup diffim
 */
template <typename ImageT, typename MaskT>
std::vector<double> diffim::computePsfMatchingKernelForFootprint_Legacy(
    double &background,                                                            
    image::MaskedImage<ImageT, MaskT> const &imageToConvolve,           
    image::MaskedImage<ImageT, MaskT> const &imageToNotConvolve,        
    math::KernelList<math::Kernel> const &kernelInBasisList, 
    lsst::pex::policy::Policy &policy                                              
    ) { 
    
    /* grab mask bits from the image to convolve, since that is what we'll be operating on */
    int edgeMaskBit = imageToConvolve.getMask()->getMaskPlane("EDGE");
    
    int nKernelParameters = 0;
    int nBackgroundParameters = 0;
    int nParameters = 0;
    
    boost::timer t;
    double time;
    t.restart();
    
    logging::TTrace<6>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Entering subroutine computePsfMatchingKernelForFootprint");
    
    /* We assume that each kernel in the Set has 1 parameter you fit for */
    nKernelParameters = kernelInBasisList.size();
    /* Or, we just assume that across a single kernel, background 0th order.  This quite makes sense. */
    nBackgroundParameters = 1;
    /* Total number of parameters */
    nParameters = nKernelParameters + nBackgroundParameters;
    
    vw::math::Vector<double> B(nParameters);
    vw::math::Matrix<double> M(nParameters, nParameters);
    for (unsigned int i = nParameters; i--;) {
        B(i) = 0;
        for (unsigned int j = nParameters; j--;) {
            M(i,j) = 0;
        }
    }
    
    /* convolve creates a MaskedImage, push it onto the back of the Vector */
    /* need to use shared pointers because MaskedImage copy does not work */
    std::vector<boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > > convolvedImageList(nKernelParameters);
    /* and an iterator over this */
    typename std::vector<boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > >::iterator citer = convolvedImageList.begin();
    
    /* Iterator for input kernel basis */
    std::vector<boost::shared_ptr<math::Kernel> >::const_iterator kiter = kernelInBasisList.begin();
    /* Create C_ij in the formalism of Alard & Lupton */
    for (; kiter != kernelInBasisList.end(); ++kiter, ++citer) {
        
        logging::TTrace<7>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                           "Convolving an Object with Basis");
        
        /* NOTE : we could also *precompute* the entire template image convolved with these functions */
        /*        and save them somewhere to avoid this step each time.  however, our paradigm is to */
        /*        compute whatever is needed on the fly.  hence this step here. */
        boost::shared_ptr<image::MaskedImage<ImageT, MaskT> > imagePtr(
                                                                       new image::MaskedImage<ImageT, MaskT>
                                                                       (math::convolveNew(imageToConvolve, **kiter, edgeMaskBit, false))
                                                                       );
        
        logging::TTrace<7>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                           "Convolved an Object with Basis");
        
        *citer = imagePtr;
        
    } 
    
    /* NOTE ABOUT CONVOLUTION : */
    /* getCtrCol:getCtrRow pixels are masked on the left:bottom side */
    /* getCols()-getCtrCol():getRows()-getCtrRow() masked on right/top side */
    /* */
    /* The convolved image and the input image are by default the same size, so */
    /* we offset our initial pixel references by the same amount */
    kiter = kernelInBasisList.begin();
    citer = convolvedImageList.begin();
    unsigned int startCol = (*kiter)->getCtrCol();
    unsigned int startRow = (*kiter)->getCtrRow();
    /* NOTE - I determined I needed this +1 by eye */
    unsigned int endCol   = (*citer)->getCols() - ((*kiter)->getCols() - (*kiter)->getCtrCol()) + 1;
    unsigned int endRow   = (*citer)->getRows() - ((*kiter)->getRows() - (*kiter)->getCtrRow()) + 1;
    /* NOTE - we need to enforce that the input images are large enough */
    /* How about some multiple of the PSF FWHM?  Or second moments? */
    
    /* An accessor for each convolution plane */
    /* NOTE : MaskedPixelAccessor has no empty constructor, therefore we need to push_back() */
    std::vector<image::MaskedPixelAccessor<ImageT, MaskT> > convolvedAccessorRowList;
    for (citer = convolvedImageList.begin(); citer != convolvedImageList.end(); ++citer) {
        convolvedAccessorRowList.push_back(image::MaskedPixelAccessor<ImageT, MaskT>(**citer));
    }
    
    /* An accessor for each input image; address rows and cols separately */
    image::MaskedPixelAccessor<ImageT, MaskT> imageToConvolveRow(imageToConvolve);
    image::MaskedPixelAccessor<ImageT, MaskT> imageToNotConvolveRow(imageToNotConvolve);
    
    /* Take into account buffer for kernel images */
    imageToConvolveRow.advance(startCol, startRow);
    imageToNotConvolveRow.advance(startCol, startRow);
    for (int ki = 0; ki < nKernelParameters; ++ki) {
        convolvedAccessorRowList[ki].advance(startCol, startRow);
    }
    
    for (unsigned int row = startRow; row < endRow; ++row) {
        
        /* An accessor for each convolution plane */
        std::vector<image::MaskedPixelAccessor<ImageT, MaskT> > convolvedAccessorColList = convolvedAccessorRowList;
        
        /* An accessor for each input image; places the col accessor at the correct row */
        image::MaskedPixelAccessor<ImageT, MaskT> imageToConvolveCol = imageToConvolveRow;
        image::MaskedPixelAccessor<ImageT, MaskT> imageToNotConvolveCol = imageToNotConvolveRow;
        
        for (unsigned int col = startCol; col < endCol; ++col) {
            
            ImageT ncImage   = *imageToNotConvolveCol.image;
            ImageT ncVariance = *imageToNotConvolveCol.variance;
            MaskT  ncMask     = *imageToNotConvolveCol.mask;
            
            ImageT cVariance  = *imageToConvolveCol.variance;
            
            /* Variance for a particlar pixel; do we use this variance of the */
            /* input data, or include the variance after its been convolved with */
            /* the basis?  For now, use the average of the input varianes. */
            ImageT iVariance  = 1.0 / (cVariance + ncVariance);
            
            logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                               "Accessing image row %d col %d : %.3f %.3f %d",
                               row, col, ncImage, ncVariance, ncMask);
            
            /* kernel index i */
            typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator
                kiteri   = convolvedAccessorColList.begin();
            typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator
                kiterEnd = convolvedAccessorColList.end();
            
            
            for (int kidxi = 0; kiteri != kiterEnd; ++kiteri, ++kidxi) {
                ImageT cdImagei   = *kiteri->image;
                
                ImageT cdVariancei = *kiteri->variance;
                MaskT  cdMaski     = *kiteri->mask;
                logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                                   "Accessing convolved image %d : %.3f %.3f %d",
                                   kidxi, cdImagei, cdVariancei, cdMaski);
                
                /* kernel index j  */
                typename std::vector<image::MaskedPixelAccessor<ImageT, MaskT> >::iterator kiterj = kiteri;
                for (int kidxj = kidxi; kiterj != kiterEnd; ++kiterj, ++kidxj) {
                    ImageT cdImagej   = *kiterj->image;
                    
                    /* NOTE - These inner trace statements can ENTIRELY kill the run time */
                    ImageT cdVariancej = *kiterj->variance;
                    MaskT  cdMaskj     = *kiterj->mask;
                    logging::TTrace<8>("lsst.ip.diffim.computePsfMatchingKernelForFootprint",
                                       "Accessing convolved image %d : %.3f %.3f %d",
                                       kidxj, cdImagej, cdVariancej, cdMaskj);
                    
                    M[kidxi][kidxj] += cdImagei * cdImagej * iVariance;
                } 
                
                B[kidxi] += ncImage * cdImagei * iVariance;
                
                /* Constant background term; effectively j=kidxj+1 */
                M[kidxi][nParameters-1] += cdImagei * iVariance;
            } 
            
            /* Background term; effectively i=kidxi+1 */
            B[nParameters-1] += ncImage * iVariance;
            M[nParameters-1][nParameters-1] += 1.0 * iVariance;
            
            logging::TTrace<7>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                               "Background terms : %.3f %.3f",
                               B[nParameters-1], M[nParameters-1][nParameters-1]);
            
            /* Step each accessor in column */
            imageToConvolveCol.nextCol();
            imageToNotConvolveCol.nextCol();
            for (int ki = 0; ki < nKernelParameters; ++ki) {
                convolvedAccessorColList[ki].nextCol();
            }             
            
        } /* col */
        
        /* Step each accessor in row */
        imageToConvolveRow.nextRow();
        imageToNotConvolveRow.nextRow();
        for (int ki = 0; ki < nKernelParameters; ++ki) {
            convolvedAccessorRowList[ki].nextRow();
        }
        
    } /* row */
    
    /* NOTE: If we are going to regularize the solution to M, this is the place to do it */
    
    /* Fill in rest of M */
    for (int kidxi=0; kidxi < nParameters; ++kidxi) 
        for (int kidxj=kidxi+1; kidxj < nParameters; ++kidxj) 
            M[kidxj][kidxi] = M[kidxi][kidxj];
    
#if DEBUG_MATRIX
    std::cout << "B : " << B << std::endl;
    std::cout << "M : " << M << std::endl;
#endif
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time before matrix inversions : %.2f s", time);
    
    /* Invert using SVD and Pseudoinverse */
    vw::math::Matrix<double> Minv;
    Minv = vw::math::pseudoinverse(M);
    /*Minv = vw::math::inverse(M); */
    
#if DEBUG_MATRIX
    std::cout << "Minv : " << Minv << std::endl;
#endif
    
    /* Solve for x in Mx = B */
    vw::math::Vector<double> Soln = Minv * B;
    
#if DEBUG_MATRIX
    std::cout << "Solution : " << Soln << std::endl;
#endif
    
    time = t.elapsed();
    logging::TTrace<5>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Total compute time after matrix inversions : %.2f s", time);
    
    /* Translate from VW std::vectors to std std::vectors */
    std::vector<double> kernelCoeffs(kernelInBasisList.size());
    for (int ki = 0; ki < nKernelParameters; ++ki) {
        kernelCoeffs[ki] = Soln[ki];
    }
    background = Soln[nParameters-1];
    
    logging::TTrace<6>("lsst.ip.diffim.computePsfMatchingKernelForFootprint", 
                       "Leaving subroutine computePsfMatchingKernelForFootprint");
    
    return kernelCoeffs;
}
#endif
