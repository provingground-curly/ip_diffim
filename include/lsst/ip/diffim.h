/**
 * \file
 * \brief An include file to include the header files for lsst::ip::diffim
 */
#ifndef LSST_IP_DIFFIM_H
#define LSST_IP_DIFFIM_H

#include "lsst/ip/diffim/BasisLists.h"
#include "lsst/ip/diffim/ImageSubtract.h"
#include "lsst/ip/diffim/KernelSolution.h"
#include "lsst/ip/diffim/KernelCandidate.h"
#include "lsst/ip/diffim/KernelCandidateDetection.h"

#include "lsst/ip/diffim/AssessSpatialKernelVisitor.h"
#include "lsst/ip/diffim/BuildSingleKernelVisitor.h"
#include "lsst/ip/diffim/BuildSpatialKernelVisitor.h"
#include "lsst/ip/diffim/KernelPcaVisitor.h"
#include "lsst/ip/diffim/KernelSumVisitor.h"

#endif // LSST_IP_DIFFIM_H
