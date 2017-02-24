from __future__ import absolute_import, division, print_function
from future import standard_library
standard_library.install_aliases()
#
# LSST Data Management System
# Copyright 2016 AURA/LSST.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <https://www.lsstcorp.org/LegalNotices/>.
#

import numpy as np

import lsst.afw.image as afwImage
import lsst.afw.math as afwMath
import lsst.afw.geom as afwGeom
import lsst.pex.config as pexConfig
import lsst.pipe.base as pipeBase

__all__ = ("ImageMapReduceTask", "ImageMapReduceConfig",
           "ImageMapperSubtask", "ImageMapperSubtaskConfig",
           "ImageReducerSubtask", "ImageReducerSubtaskConfig")


class ImageMapperSubtaskConfig(pexConfig.Config):
    """!
    \anchor ImageMapperSubtaskConfig_

    \brief Configuration parameters for the ImageMapperSubtask
    """
    pass


class ImageMapperSubtask(pipeBase.Task):
    """!
    \anchor ImageMapperSubtask_

    \brief Simple example and base class for any task that is to be
    used as `ImageMapReduceConfig.mapperSubtask`

    """
    ConfigClass = ImageMapperSubtaskConfig
    _DefaultName = "ip_diffim_ImageMapperSubtask"

    def __init__(self, *args, **kwargs):
        """! Create the image gridding subTask
        @param *args arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        @param **kwargs keyword arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        """
        pipeBase.Task.__init__(self, *args, **kwargs)

    def run(self, subExp, expandedSubExp, fullBBox, **kwargs):
        """! Perform operation on given sub-xposure

        @param subExp the sub-exposure upon which to
        operate
        @param expandedSubExp the expanded sub-exposure
        upon which to operate
        @param fullBBox the bounding box of the original exposure
        @param **kwargs additional keyword arguments
        @return anything, including an a `afw.Exposure`
        """
        pass


class ImageReducerSubtaskConfig(pexConfig.Config):
    """!
    \anchor ImageReducerSubtaskConfig_

    \brief Configuration parameters for the ImageReducerSubtask
    """
    reduceOperation = pexConfig.ChoiceField(
        dtype=str,
        doc="""Operation to use for reducing subimages into new image.""",
        default="copy",
        allowed={
            "none": """simply return a list of values and not re-map results into
                       a new image (noop operation)""",
            "copy": """copy pixels directly from subimage (non-deterministic for overlaps)
                       into correct location in new exposure""",
            "sum": """add pixels from overlaps (probably never wanted; used for testing)
                       into correct location in new exposure""",
            "average": """same as copy, but average pixels from overlaps"""
        }
    )


class ImageReducerSubtask(pipeBase.Task):
    """!
    \anchor ImageReducerSubtask_

    \brief Simple example and base class for any task that is to be
    used as `ImageMapReduceConfig.reducerSubtask`

    """
    ConfigClass = ImageReducerSubtaskConfig
    _DefaultName = "ip_diffim_ImageReducerSubtask"

    def __init__(self, *args, **kwargs):
        """! Create the image gridding subTask
        @param *args arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        @param **kwargs keyword arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        """
        pipeBase.Task.__init__(self, *args, **kwargs)

    def run(self, patches, exposure, **kwargs):
        """! Reduce a set of sub-exposure patches into a final exposure

        Return an exposure of the same dimensions as `exposure`.

        @param patches list of subExposures of `exposure` to merge
        @param exposure the original exposure which is used as the template
        @param **kwargs additional keyword arguments
        @return a `afw.Exposure`

        @note Notes and currently known issues:
        1. This currently *should* correctly handle overlapping patches.
           For overlapping patches, use `config.reduceOperation='average'.
        2. This currently does not correctly handle varying PSFs (in fact,
           it just copies over the PSF from the original exposure)
        3. This logic currently makes *two* copies of the original exposure
           (one here and one in `mapperSubtask.run()`).
        """
        if self.config.reduceOperation == 'none':
            return patches  # this is likely a non-image-type, such as a list of floats.

        newExp = afwImage.ExposureF(exposure).clone()
        newMI = newExp.getMaskedImage()
        newMI.getImage()[:, :] = 0.
        newMI.getVariance()[:, :] = 0.

        reduceOp = self.config.reduceOperation
        if reduceOp == 'average':  # make an array to keep track of weights
            weights = afwImage.ImageF(newMI.getBBox())  # Needs to be a float to divide later.

        for patch in patches:
            subExp = afwImage.ExposureF(newExp, patch.getBBox())
            subMI = subExp.getMaskedImage()
            patchMI = patch.getMaskedImage()
            if reduceOp == 'copy':
                subMI[:, :] = patchMI
            elif reduceOp == 'sum':
                subMI += patchMI
            elif reduceOp == 'average':
                subMI += patchMI
                wsubim = afwImage.ImageF(weights, patch.getBBox())
                wsubim += 1.

        if reduceOp == 'average':
            newMI /= weights
        return newExp


class ImageMapReduceConfig(pexConfig.Config):
    """!
    \anchor ImageMapReduceConfig_

    \brief Configuration parameters for the ImageMapReduceTask
    """

    mapperSubtask = pexConfig.ConfigurableField(
        doc="Subtask to run on each subimage",
        target=ImageMapperSubtask,
    )

    reducerSubtask = pexConfig.ConfigurableField(
        doc="Subtask to combine results of mapperSubTask",
        target=ImageReducerSubtask,
    )

    # Separate gridCentroidsX and gridCentroidsY since pexConfig.ListField accepts limited dtypes
    #  (i.e., no Point2D)
    gridCentroidsX = pexConfig.ListField(
        dtype=float,
        doc="Input X centroids around which to place subimages. If None, use grid config options below.",
        optional=True,
        default=None
    )

    gridCentroidsY = pexConfig.ListField(
        dtype=float,
        doc="Input Y centroids around which to place subimages. If None, use grid config options below.",
        optional=True,
        default=None
    )

    gridSizeX = pexConfig.Field(
        dtype=float,
        doc="""Dimensions of each grid cell in x direction""",
        default=10.,
        check=lambda x: x > 0.
    )

    gridSizeY = pexConfig.Field(
        dtype=float,
        doc="""Dimensions of each grid cell in y direction""",
        default=10.,
        check=lambda x: x > 0.
    )

    gridStepX = pexConfig.Field(
        dtype=float,
        doc="""Spacing between subsequent grid cells in x direction. If equal to
               gridSizeX, then there is no overlap in the x direction.""",
        default=10.,
        check=lambda x: x > 0.
    )

    gridStepY = pexConfig.Field(
        dtype=float,
        doc="""Spacing between subsequent grid cells in y direction. If equal to
               gridSizeY, then there is no overlap in the y direction.""",
        default=10.,
        check=lambda x: x > 0.
    )

    borderSizeX = pexConfig.Field(
        dtype=float,
        doc="""Dimensions of cell border in +/- x direction""",
        default=5.,
        check=lambda x: x > 0.
    )

    borderSizeY = pexConfig.Field(
        dtype=float,
        doc="""Dimensions of cell border in +/- y direction""",
        default=5.,
        check=lambda x: x > 0.
    )

    adjustGridOption = pexConfig.ChoiceField(
        dtype=str,
        doc="""Whether and how to adjust grid to fit evenly within image""",
        default="spacing",
        allowed={
            "spacing": "adjust spacing between centers of grid cells (allowing overlaps)",
            "size": "adjust the sizes of the grid cells (disallowing overlaps)",
            "none": "do not adjust the grid sizes or spacing"
        }
    )

    scaleByFwhm = pexConfig.Field(
        dtype=bool,
        doc="""Scale gridSize/gridStep/borderSize/overlapSize by PSF FWHM rather
        than pixels?""",
        default=True
    )

    ignoreMaskPlanes = pexConfig.ListField(
        dtype=str,
        doc="""Mask planes to ignore for sigma-clipped statistics""",
        default=("INTRP", "EDGE", "DETECTED", "SAT", "CR", "BAD", "NO_DATA", "DETECTED_NEGATIVE")
    )


## \addtogroup LSST_task_documentation
## \{
## \page ImageMapReduceTask
## \ref ImageMapReduceTask_ "ImageMapReduceTask"
##      Task for performing operations on an image over a regular-spaced grid
## \}


class ImageMapReduceTask(pipeBase.Task):
    """!
    \anchor ImageMapReduceTask_

    \brief Break an image in to subimages on a grid and perform
    the same operation on each.

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Contents Contents

      - \ref ip_diffim_imageMapReduce_ImageMapReduceTask_Purpose
      - \ref ip_diffim_imageMapReduce_ImageMapReduceTask_Config
      - \ref ip_diffim_imageMapReduce_ImageMapReduceTask_Run
      - \ref ip_diffim_imageMapReduce_ImageMapReduceTask_Debug
      - \ref ip_diffim_imageMapReduce_ImageMapReduceTask_Example

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Purpose	Description

    Task that can perform 'simple' operations on a gridded set of
    subimages of a larger image, and then have those subimages
    stitched back together into a new, full-sized modified image.

    The actual operation is performed by a subTask passed to the
    config. The input exposure will be pre-subimaged, but the `run`
    method of the subtask will also have access to the entire
    (original) image. The reducing operation is also handled by a
    subtask.

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Initialize       Task initialization

    \copydoc \_\_init\_\_

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Run       Invoking the Task

    \copydoc run

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Config       Configuration parameters

    See \ref ImageMapReduceConfig

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Debug		Debug variables

    This task has no debug variables

    \section ip_diffim_imageMapReduce_ImageMapReduceTask_Example	Example of using ImageMapReduceTask

    This task has an example simple implementation of the use of
    ImageMapperSubtask/Config in its unit test
    \link tests/testImageMapReduce testImageMapReduce\endlink.
    """
    ConfigClass = ImageMapReduceConfig
    _DefaultName = "ip_diffim_imageMapReduce"

    def __init__(self, *args, **kwargs):
        """! Create the image gridding Task
        @param *args arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        @param **kwargs keyword arguments to be passed to
        lsst.pipe.base.task.Task.__init__
        """
        pipeBase.Task.__init__(self, *args, **kwargs)

        self.boxes0 = self.boxes1 = None
        self.makeSubtask("mapperSubtask")
        self.makeSubtask("reducerSubtask")

    @pipeBase.timeMethod
    def run(self, exposure, **kwargs):
        """! Perform a map-reduce operation on the given exposure.

        Break the exposure into sub-exps on a grid (parameters given
        by `ImageMapReduceConfig`) and perform `mapperSubtask.run()` on
        each. Reduce the resulting sub-exps by running `reducerSubtask.run()`.

        @param exposure the full exposure to process
        @param **kwargs additional keyword arguments
        @return output of `reducerSubtask.run()`

        """
        self.log.info("Processing.")

        patches = self._runMapper(exposure, **kwargs)
        newMI = self._reduceImage(patches, exposure, **kwargs)
        return newMI

    def _runMapper(self, exposure, doClone=False, **kwargs):
        """! Perform `mapperSubtask.run()` on each patch

        Perform `mapperSubtask.run()` on each patch across a grid on `exposure`
        generated by `generateGrid`.

        @param exposure the original exposure which is used as the template.
        @param doClone if True, clone the subimages before passing to subtask;
        @param **kwargs additional keyword arguments
        in that case, the sub-exps do not have to be considered as read-only.
        @return a list of `afwExposure`s
        """
        boxes0, boxes1 = self.boxes0, self.boxes1
        if boxes0 is None:
            boxes0, boxes1 = self._generateGrid(exposure)
        if len(boxes0) != len(boxes1):
            raise Exception('Uh oh!')   # TBD: define a specific exception to raise

        self.log.info("Processing %d sub-exposures" % len(boxes0))
        patches = []
        for i in range(len(boxes0)):
            subExp = afwImage.ExposureF(exposure, boxes0[i])
            expandedSubExp = afwImage.ExposureF(exposure, boxes1[i])
            if doClone:
                subExp = subExp.clone()
                expandedSubExp = expandedSubExp.clone()
            result = self.mapperSubtask.run(subExp, expandedSubExp, exposure.getBBox(), **kwargs)
            patches.append(result)

        return patches

    def _reduceImage(self, patches, exposure, **kwargs):
        """! Reduce/merge a set of sub-exposure patches into a final exposure

        Return an exposure of the same dimensions as `exposure`.
        `patches` is expected to have been produced by `runMapper`.

        @param patches list of subExposures of `exposure` to merge
        @param exposure the original exposure
        @param **kwargs additional keyword arguments
        @return a `afw.Exposure`
        """
        result = self.reducerSubtask.run(patches, exposure, **kwargs)
        return result

    def _generateGrid(self, exposure, forceEvenSized=False):
        """! Generate two lists of bounding boxes that evenly grid `exposure`

        Unless the config was provided with `centroidCoordsX` and
        `centroidCoordsY`, grid (subimage) centers are spaced
        evenly by gridStepX/Y. Then the grid is adjusted as
        little as possible to evenly cover the input exposure (if
        adjustGridOption is not 'none'). Then the second set of
        bounding boxes is expanded by borderSizeX/Y. The expanded
        bounding boxes are adjusted to ensure that they intersect
        the exposure's bounding box. The resulting lists of bounding
        boxes and corresponding expanded bounding boxes are
        returned, and also set to `self.boxes0`, `self.boxes1`.

        @param exposure an `afwImage.Exposure` whose full bounding
        box is to be evenly gridded.
        @param forceEvenSized force grid elements to have even x- and
        y- dimensions (for ZOGY)
        @return tuple containing two lists of `afwGeom.BoundingBox`es
        """
        # Extract the config parameters for conciseness.
        gridSizeX = self.config.gridSizeX
        gridSizeY = self.config.gridSizeY
        gridStepX = self.config.gridStepX
        gridStepY = self.config.gridStepY
        borderSizeX = self.config.borderSizeX
        borderSizeY = self.config.borderSizeY
        adjustGridOption = self.config.adjustGridOption
        scaleByFwhm = self.config.scaleByFwhm
        bbox = exposure.getBBox()

        psfFwhm = (exposure.getPsf().computeShape().getDeterminantRadius() *
                   2. * np.sqrt(2. * np.log(2.)))

        def rescaleValue(val):
            if scaleByFwhm:
                return np.rint(val * psfFwhm).astype(int)
            else:
                return np.rint(val).astype(int)

        gridSizeX = rescaleValue(gridSizeX)
        if gridSizeX > bbox.getWidth():
            gridSizeX = bbox.getWidth()
        gridSizeY = rescaleValue(gridSizeY)
        if gridSizeY > bbox.getHeight():
            gridSizeY = bbox.getHeight()
        gridStepX = rescaleValue(gridStepX)
        if gridStepX > bbox.getWidth():
            gridStepX = bbox.getWidth()
        gridStepY = rescaleValue(gridStepY)
        if gridStepY > bbox.getHeight():
            gridStepY = bbox.getHeight()
        borderSizeX = rescaleValue(borderSizeX)
        borderSizeY = rescaleValue(borderSizeY)

        nGridX = bbox.getWidth() // gridStepX
        nGridY = bbox.getHeight() // gridStepY

        if adjustGridOption == 'spacing':
            nGridX = bbox.getWidth() / gridStepX
            # Readjust gridStepX so that it fits perfectly in the image.
            gridStepX = float(bbox.getWidth() - gridSizeX) / float(nGridX)
            if gridStepX <= 0:
                gridStepX = 1

        if adjustGridOption == 'spacing':
            nGridY = bbox.getWidth() / gridStepY
            # Readjust gridStepY so that it fits perfectly in the image.
            gridStepY = float(bbox.getHeight() - gridSizeY) / float(nGridY)
            if gridStepY <= 0:
                gridStepY = 1

        # first "main" box at 0,0
        bbox0 = afwGeom.Box2I(afwGeom.Point2I(bbox.getBegin()), afwGeom.Extent2I(gridSizeX, gridSizeY))
        # first expanded box
        bbox1 = afwGeom.Box2I(bbox0)
        bbox1.grow(afwGeom.Extent2I(borderSizeX, borderSizeY))

        self.boxes0 = []  # "main" boxes; store in task so can be extracted if needed
        self.boxes1 = []  # "expanded" boxes

        # use given centroids as centers for bounding boxes
        if self.config.gridCentroidsX is not None and len(self.config.gridCentroidsX) > 0:
            for i, centroidX in enumerate(self.config.gridCentroidsX):
                centroidY = self.config.gridCentroidsY[i]
                centroid = afwGeom.Point2D(centroidX, centroidY)
                bb0 = afwGeom.Box2I(bbox0)
                xoff = int(np.floor(centroid.getX())) - bb0.getWidth()//2
                yoff = int(np.floor(centroid.getY())) - bb0.getHeight()//2
                bb0.shift(afwGeom.Extent2I(xoff, yoff))
                bb0.clip(bbox)
                self.boxes0.append(bb0)
                bb1 = afwGeom.Box2I(bbox1)
                bb1.shift(afwGeom.Extent2I(xoff, yoff))
                bb1.clip(bbox)
                self.boxes1.append(bb1)
            return self.boxes0, self.boxes1

        # Offset the "main" (bbox0) and "expanded" (bbox1) bboxes by xoff, yoff.
        # Clip them by the exposure's bbox.
        def offsetAndClipBoxes(bbox0, bbox1, xoff, yoff, bbox):
            xoff = int(np.floor(xoff))
            yoff = int(np.floor(yoff))
            bb0 = afwGeom.Box2I(bbox0)
            bb0.shift(afwGeom.Extent2I(xoff, yoff))
            bb0.clip(bbox)
            bb1 = afwGeom.Box2I(bbox1)
            bb1.shift(afwGeom.Extent2I(xoff, yoff))
            bb1.clip(bbox)
            if forceEvenSized:
                bb0.grow(afwGeom.Extent2I(bb0.getWidth() % 2, bb0.getHeight() % 2))
                bb1.grow(afwGeom.Extent2I(bb1.getWidth() % 2, bb1.getHeight() % 2))
            return bb0, bb1

        xoff = 0
        while(xoff <= bbox.getWidth()):
            yoff = 0
            while(yoff <= bbox.getHeight()):
                bb0, bb1 = offsetAndClipBoxes(bbox0, bbox1, xoff, yoff, bbox)
                yoff += gridStepY
                if bb0.getArea() > 0 and bb1.getArea() > 0:
                    self.boxes0.append(bb0)
                    self.boxes1.append(bb1)
            xoff += gridStepX

        return self.boxes0, self.boxes1

    def _plotBoxes(self, exposure):
        """! Plot both grids of boxes using matplotlib.

        `self.boxes0` and `self.boxes1` must have been set.
        @param exposure an input afwImage.Exposure
        """
        import matplotlib.pyplot as plt

        bbox = exposure.getBBox()
        if self.boxes0 is None:
            boxes0, boxes1 = self._generateGrid(exposure)
        self._plotBoxGrid(boxes0[::3], bbox, ls='--')
        # reset the color cycle -- see
        # http://stackoverflow.com/questions/24193174/reset-color-cycle-in-matplotlib
        plt.gca().set_prop_cycle(None)
        self._plotBoxGrid(boxes1[::3], bbox, ls=':')

    def _plotBoxGrid(self, boxes, bbox, **kwargs):
        """! Plot a grid of boxes using matplotlib.

        @param boxes a list of `afwGeom.BoundingBox`es
        @param bbox an overall bounding box
        @param **kwargs additional keyword arguments
        """
        import matplotlib.pyplot as plt

        def plotBox(box):
            corners = np.array([np.array([pt.getX(), pt.getY()]) for pt in box.getCorners()])
            corners = np.vstack([corners, corners[0, :]])
            plt.plot(corners[:, 0], corners[:, 1], **kwargs)

        for b in boxes:
            plotBox(b)
        plt.xlim(bbox.getBeginX(), bbox.getEndX())
        plt.ylim(bbox.getBeginY(), bbox.getEndY())
