from cc3d import CompuCellSetup
from .BoundaryPixelTrackerVisSteppables import BoundaryPixelTrackerSteppable

CompuCellSetup.register_steppable(steppable=BoundaryPixelTrackerSteppable(frequency=10))

CompuCellSetup.run()

