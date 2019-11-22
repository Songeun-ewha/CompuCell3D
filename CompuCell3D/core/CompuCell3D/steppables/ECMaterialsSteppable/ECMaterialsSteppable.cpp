/*************************************************************************
*    CompuCell - A software framework for multimodel simulations of     *
* biocomplexity problems Copyright (C) 2003 University of Notre Dame,   *
*                             Indiana                                   *
*                                                                       *
* This program is free software; IF YOU AGREE TO CITE USE OF CompuCell  *
*  IN ALL RELATED RESEARCH PUBLICATIONS according to the terms of the   *
*  CompuCell GNU General Public License RIDER you can redistribute it   *
* and/or modify it under the terms of the GNU General Public License as *
*  published by the Free Software Foundation; either version 2 of the   *
*         License, or (at your option) any later version.               *
*                                                                       *
* This program is distributed in the hope that it will be useful, but   *
*      WITHOUT ANY WARRANTY; without even the implied warranty of       *
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU    *
*             General Public License for more details.                  *
*                                                                       *
*  You should have received a copy of the GNU General Public License    *
*     along with this program; if not, write to the Free Software       *
*      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.        *
*************************************************************************/
#include <CompuCell3D/CC3D.h>

using namespace CompuCell3D;
using namespace std;

/**
@author T.J. Sego, Ph.D.
*/

#include "ECMaterialsSteppable.h"
#include "CompuCell3D/plugins/ECMaterials/ECMaterialsPlugin.h"

ECMaterialsSteppable::ECMaterialsSteppable() : 
	cellFieldG(0),
	sim(0),
	potts(0),
	xmlData(0),
	boundaryStrategy(0),
	automaton(0),
	cellInventoryPtr(0),
	pUtils(0),
	numberOfMaterials(0),
	ECMaterialsInitialized(false),
	AnyInteractionsDefined(false),
	MaterialInteractionsDefined(false),
	FieldInteractionsDefined(false),
	CellInteractionsDefined(false)
{}

ECMaterialsSteppable::~ECMaterialsSteppable() {
	pUtils->destroyLock(lockPtr);
	delete lockPtr;
	lockPtr = 0;
}

void ECMaterialsSteppable::init(Simulator *simulator, CC3DXMLElement *_xmlData) {
    xmlData=_xmlData;
    potts = simulator->getPotts();
    cellInventoryPtr=& potts->getCellInventory();
    sim=simulator;

	pUtils = sim->getParallelUtils();
	lockPtr = new ParallelUtilsOpenMP::OpenMPLock_t;
	pUtils->initLock(lockPtr);

	bool pluginAlreadyRegisteredFlag;

	// Get boundary pixel tracker plugin
	boundaryTrackerPlugin = (BoundaryPixelTrackerPlugin*)Simulator::pluginManager.get("BoundaryPixelTracker", &pluginAlreadyRegisteredFlag);
	if (!pluginAlreadyRegisteredFlag) {
		CC3DXMLElement *BoundaryPixelTrackerXML = simulator->getCC3DModuleData("Plugin", "BoundaryPixelTracker");
		boundaryTrackerPlugin->init(simulator, BoundaryPixelTrackerXML);
	}

    cellFieldG = (WatchableField3D<CellG *> *)potts->getCellFieldG();
    fieldDim=cellFieldG->getDim();
    simulator->registerSteerableObject(this);

    update(_xmlData,true);

	pUtils->unsetLock(lockPtr);

}

void ECMaterialsSteppable::extraInit(Simulator *simulator){

    //PUT YOUR CODE HERE
}

void ECMaterialsSteppable::handleEvent(CC3DEvent & _event) {
	if (_event.id == LATTICE_RESIZE) {

		if (ECMaterialsInitialized) {
			pUtils->setLock(lockPtr);
			constructNeighborPtStorage();
			pUtils->unsetLock(lockPtr);
		}

	}
}

void ECMaterialsSteppable::start(){

    //PUT YOUR CODE HERE

}

void ECMaterialsSteppable::step(const unsigned int currentStep){

	if (!ECMaterialsInitialized) {
		pUtils->setLock(lockPtr);
		update(xmlData, true);
		pUtils->unsetLock(lockPtr);
	}

	if (!AnyInteractionsDefined) return;

    // Make a copy of the ECMaterialsField
	ECMaterialsField = ecMaterialsPlugin->getECMaterialField();
	Field3D<ECMaterialsData *> *ECMaterialsFieldOld = (Field3D<ECMaterialsData *>*) new WatchableField3D<ECMaterialsData *>(fieldDim, 0);
	ECMaterialsFieldOld = ECMaterialsField;

	boundaryStrategy = BoundaryStrategy::getInstance();
	int nNeighbors = boundaryStrategy->getMaxNeighborIndexFromNeighborOrder(1);

	std::vector<Point3D>::iterator ptItr;
	std::vector<int>::iterator intItr1;
	std::vector<int>::iterator intItr2;

	Neighbor neighbor;
	Point3D pt(0, 0, 0);
	Point3D nPt(0, 0, 0);
	std::vector<float> qtyOld;
	std::vector<float> qtyNew;
	std::vector<float> qtyNeighbor;
	float materialDiffusionCoefficient;
	float thisFieldVal;
	int numberOfReactionsDefined = idxMaterialReactionsDefined.size();
	int numberOfDiffusionDefined = idxMaterialDiffusionDefined.size();
	int neighborPtIdx = 0;

	if (MaterialInteractionsDefined || FieldInteractionsDefined) {

		for (int z = 0; z < fieldDim.z; ++z) {
			for (int y = 0; y < fieldDim.y; ++y) {
				for (int x = 0; x < fieldDim.x; ++x) {
					pt = Point3D(x, y, z);

					if (cellFieldG->get(pt)) continue;

					neighborPtIdx = x + fieldDim.x*(y + z*fieldDim.y);

					qtyOld = ECMaterialsFieldOld->get(pt)->getECMaterialsQuantityVec();
					qtyNew = qtyOld;

					// Material interactions

					if (MaterialInteractionsDefined) {

						// Material reactions with at site
						if (MaterialReactionsDefined) {
							for (intItr1 = idxMaterialReactionsDefined.begin(); intItr1 != idxMaterialReactionsDefined.end(); ++intItr1) {
								for (intItr2 = idxidxMaterialReactionsDefined[*intItr1].begin(); intItr2 != idxidxMaterialReactionsDefined[*intItr1].end(); ++intItr2) {

									qtyNew[*intItr1] += materialReactionCoefficientsByIndex[*intItr1][*intItr2][0] * qtyOld[*intItr2];

								}
							}
						}

						for (ptItr = neighborPtStorage[neighborPtIdx].begin(); ptItr != neighborPtStorage[neighborPtIdx].end(); ++ptItr) {

							if (cellFieldG->get(*ptItr)) continue; // Detect extracellular sites

							qtyNeighbor = ECMaterialsFieldOld->get(*ptItr)->getECMaterialsQuantityVec();

							// Material reactions with neighbors
							if (MaterialReactionsDefined) {
								for (intItr1 = idxMaterialReactionsDefined.begin(); intItr1 != idxMaterialReactionsDefined.end(); ++intItr1) {
									for (intItr2 = idxidxMaterialReactionsDefined[*intItr1].begin(); intItr2 != idxidxMaterialReactionsDefined[*intItr1].end(); ++intItr2) {
										qtyNew[*intItr1] += materialReactionCoefficientsByIndex[*intItr1][*intItr2][1] * qtyNeighbor[*intItr2];
									}
								}
							}

							// Material diffusion
							if (MaterialDiffusionDefined) {
								for (intItr1 = idxMaterialDiffusionDefined.begin(); intItr1 != idxMaterialDiffusionDefined.end(); ++intItr1) {
									materialDiffusionCoefficient = ECMaterialsVec->at(*intItr1).getMaterialDiffusionCoefficient();
									qtyNew[*intItr1] -= materialDiffusionCoefficient*qtyOld[*intItr1];
									qtyNew[*intItr1] += materialDiffusionCoefficient*qtyNeighbor[*intItr1];
								}
							}

						}

					}

					// Field interactions

					if (FieldInteractionsDefined) {

						// Update quantities
						for (intItr1 = idxFromFieldInteractionsDefined.begin(); intItr1 != idxFromFieldInteractionsDefined.end(); ++intItr1) {
							for (intItr2 = idxidxFromFieldInteractionsDefined[*intItr1].begin(); intItr2 != idxidxFromFieldInteractionsDefined[*intItr1].end(); ++intItr2) {
								qtyNew[*intItr1] += fromFieldReactionCoefficientsByIndex[*intItr1][*intItr2] * fieldVec[*intItr2]->get(pt);
							}
						}

						// Generate field source terms
						// calculateMaterialToFieldInteractions(pt, qtyOld);
						for (intItr1 = idxToFieldInteractionsDefined.begin(); intItr1 != idxToFieldInteractionsDefined.end(); ++intItr1) {
							thisFieldVal = fieldVec[*intItr1]->get(pt);

							for (intItr2 = idxidxToFieldInteractionsDefined[*intItr1].begin(); intItr2 != idxidxToFieldInteractionsDefined[*intItr1].end(); ++intItr2) {
								thisFieldVal += toFieldReactionCoefficientsByIndex[*intItr1][*intItr2] * qtyOld[*intItr2];
							}
							if (thisFieldVal < 0.0) thisFieldVal = 0.0;

							fieldVec[*intItr1]->set(pt, thisFieldVal);

						}

					}

					ECMaterialsField->get(pt)->setECMaterialsQuantityVec(checkQuantities(qtyNew));

				}
			}
		}

	}

	// Cell interactions
	if (CellInteractionsDefined) { 
		
		calculateCellInteractions(ECMaterialsFieldOld);

	}

}

void ECMaterialsSteppable::update(CC3DXMLElement *_xmlData, bool _fullInitFlag){

	// Do this to enable initializations independently of startup routine
	if (ECMaterialsInitialized)
		return;

	automaton = potts->getAutomaton();
	ASSERT_OR_THROW("CELL TYPE PLUGIN WAS NOT PROPERLY INITIALIZED YET. MAKE SURE THIS IS THE FIRST PLUGIN THAT YOU SET", automaton);

	// Get ECMaterials plugin
	bool pluginAlreadyRegisteredFlag;
	ecMaterialsPlugin = (ECMaterialsPlugin*)Simulator::pluginManager.get("ECMaterials", &pluginAlreadyRegisteredFlag);
	if (!pluginAlreadyRegisteredFlag) {
		return;
	}

	// Initialize preliminaries from ECMaterials plugin
	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();
	ECMaterialsField = ecMaterialsPlugin->getECMaterialField();
	ECMaterialsVec = ecMaterialsPlugin->getECMaterialsVecPtr();

	// Initialize preallocation and performance variables
	std::vector<bool> hasMaterialReactionsDefined = std::vector<bool>(numberOfMaterials, false);
	std::vector<bool> hasMaterialDiffusionDefined = std::vector<bool>(numberOfMaterials, false);
	std::vector<bool> hasToFieldInteractionsDefined;
	std::vector<bool> hasFromFieldInteractionsDefined = std::vector<bool>(numberOfMaterials, false);
	std::vector<bool> hasCellInteractionsDefined = std::vector<bool>(numberOfMaterials, false);

	idxMaterialReactionsDefined.clear();
	idxidxMaterialReactionsDefined.clear();
	idxidxMaterialReactionsDefined.resize(numberOfMaterials);
	idxMaterialDiffusionDefined.clear();
	idxToFieldInteractionsDefined.clear();
	idxidxToFieldInteractionsDefined.clear();
	idxFromFieldInteractionsDefined.clear();
	idxidxFromFieldInteractionsDefined.clear();
	idxidxFromFieldInteractionsDefined.resize(numberOfMaterials);
	idxCellInteractionsDefined.clear();
	constructNeighborPtStorage();

    // Gather XML user specifications
	CC3DXMLElementList MaterialInteractionXMLVec = xmlData->getElements("MaterialInteraction");
	CC3DXMLElementList FieldInteractionXMLVec = xmlData->getElements("FieldInteraction");
	CC3DXMLElementList MaterialDiffusionXMLVec = xmlData->getElements("MaterialDiffusion");
	CC3DXMLElementList CellInteractionXMLVec = xmlData->getElements("CellInteraction");

	std::string thisMaterialName;
	std::string catalystName;
	float coeff;
	
	MaterialInteractionsDefined = false;

	int materialIndex;
	int catalystIndex;
	if (xmlData->findElement("MaterialInteraction")) {
		MaterialReactionsDefined = true;

		cerr << "Getting ECMaterial interactions..." << endl;

		for (int XMLidx = 0; XMLidx < MaterialInteractionXMLVec.size(); ++XMLidx) {

			thisMaterialName = MaterialInteractionXMLVec[XMLidx]->getAttribute("ECMaterial");

			cerr << "   ECMaterial " << thisMaterialName << endl;

			catalystName = MaterialInteractionXMLVec[XMLidx]->getFirstElement("Catalyst")->getText();
			cerr << "   Catalyst " << catalystName << endl;

			coeff = (float) MaterialInteractionXMLVec[XMLidx]->getFirstElement("ConstantCoefficient")->getDouble();
			cerr << "   Constant coefficient " << coeff << endl;

			int interactionOrder = 0;
			if (MaterialInteractionXMLVec[XMLidx]->findElement("NeighborhoodOrder")) {
				interactionOrder = MaterialInteractionXMLVec[XMLidx]->getFirstElement("NeighborhoodOrder")->getInt();
			}
			
			materialIndex = getECMaterialIndexByName(thisMaterialName);

			ASSERT_OR_THROW("ECMaterial not defined in ECMaterials plugin.", materialIndex >= 0);

			catalystIndex = getECMaterialIndexByName(catalystName);

			ASSERT_OR_THROW("Catalyst not defined in ECMaterials plugin.", catalystIndex >= 0);

			cerr << "   ";
			ECMaterialsVec->at(materialIndex).setMaterialReactionCoefficientByName(catalystName, coeff, interactionOrder);
			hasMaterialReactionsDefined[materialIndex] = true;

			idxidxMaterialReactionsDefined[materialIndex].push_back(catalystIndex);

		}

		for (int mtlIdx = 0; mtlIdx < numberOfMaterials; ++mtlIdx) if (hasMaterialReactionsDefined[mtlIdx]) idxMaterialReactionsDefined.push_back(mtlIdx); 

	}
	else MaterialReactionsDefined = false;

	if (xmlData->findElement("MaterialDiffusion")) {
		MaterialDiffusionDefined = true;

		cerr << "Getting ECMaterial diffusion..." << endl;

		for (int XMLidx = 0; XMLidx < MaterialDiffusionXMLVec.size(); ++XMLidx) {

			thisMaterialName = MaterialDiffusionXMLVec[XMLidx]->getAttribute("ECMaterial");

			cerr << "   ECMaterial " << thisMaterialName << endl;

			catalystName = thisMaterialName;

			coeff = (float)MaterialDiffusionXMLVec[XMLidx]->getDouble();
			cerr << "      Diffusion coefficient " << coeff << endl;

			ASSERT_OR_THROW("Invalid diffusion coefficient: must be positive.", coeff >= 0);

			materialIndex = getECMaterialIndexByName(thisMaterialName);

			ASSERT_OR_THROW("ECMaterial not defined in ECMaterials plugin.", materialIndex >= 0);

			ECMaterialsVec->at(materialIndex).setMaterialDiffusionCoefficient(coeff);
			hasMaterialDiffusionDefined[materialIndex] = true;

		}
		
		for (int mtlIdx = 0; mtlIdx < numberOfMaterials; ++mtlIdx) if (hasMaterialDiffusionDefined[mtlIdx]) idxMaterialDiffusionDefined.push_back(mtlIdx);

	}
	else MaterialDiffusionDefined = false;

	MaterialInteractionsDefined = MaterialReactionsDefined || MaterialDiffusionDefined;

	if (MaterialInteractionsDefined) constructMaterialReactionCoefficients();

	if (xmlData->findElement("FieldInteraction")) {
		FieldInteractionsDefined = true;

		cerr << "Getting ECMaterial field interactions..." << endl;

		map<string, Field3D<float>*> & nameFieldMap = sim->getConcentrationFieldNameMap();

		fieldVec.clear();
		fieldNames.clear();
		for (std::map<string, Field3D<float>*>::iterator itr = nameFieldMap.begin(); itr != nameFieldMap.end(); ++itr) {
			fieldNames.insert(make_pair(itr->first, fieldNames.size()));
			fieldVec.push_back(itr->second);
		}
		numberOfFields = fieldVec.size();
		hasToFieldInteractionsDefined.assign(numberOfFields, false);
		idxidxToFieldInteractionsDefined.resize(numberOfFields);

		std::string reactantName;
		std::string productName;
		bool FieldIsDefined;
		bool toField;
		std::string thisFieldName;
		int fieldIndex;
		for (int XMLidx = 0; XMLidx < FieldInteractionXMLVec.size(); ++XMLidx) {

			cerr << "   Getting field interaction " << XMLidx + 1 << endl;

			ASSERT_OR_THROW("A reactant must be defined (using Reactant)", FieldInteractionXMLVec[XMLidx]->findElement("Reactant"));
			reactantName = FieldInteractionXMLVec[XMLidx]->getFirstElement("Reactant")->getText();
			cerr << "      Reactant " << reactantName << endl;

			ASSERT_OR_THROW("A catalyst must be defined (using Catalyst)", FieldInteractionXMLVec[XMLidx]->findElement("Catalyst"));
			catalystName = FieldInteractionXMLVec[XMLidx]->getFirstElement("Catalyst")->getText();
			cerr << "      Catalyst " << catalystName << endl;

			ASSERT_OR_THROW("A constant coefficient must be defined (using ConstantCoefficient)", FieldInteractionXMLVec[XMLidx]->findElement("ConstantCoefficient"));
			coeff = (float)FieldInteractionXMLVec[XMLidx]->getFirstElement("ConstantCoefficient")->getDouble();
			cerr << "      Constant coefficient " << coeff << endl;

			FieldIsDefined = false;

			// get field
			//		when generating field source terms
			fieldIndex = getFieldIndexByName(reactantName);
			if (fieldIndex >= 0) {
				FieldIsDefined = true;
				toField = true;
				thisFieldName = reactantName;
				thisMaterialName = catalystName;
			}
			//		when generating material source terms
			if (getFieldIndexByName(catalystName) >= 0) {

				ASSERT_OR_THROW("Cannot define field-field interactions here.", !FieldIsDefined);

				fieldIndex = getFieldIndexByName(catalystName);
				FieldIsDefined = true;
				toField = false;
				thisFieldName = catalystName;
				thisMaterialName = reactantName;
			}

			ASSERT_OR_THROW("Field " + thisFieldName + " not registered.", FieldIsDefined);

			// get material
			materialIndex = getECMaterialIndexByName(thisMaterialName);

			ASSERT_OR_THROW("ECMaterial " + thisMaterialName + " not registered in ECMaterials plugin.", materialIndex >= 0);

			// load valid info

			if (toField) {
				cerr << "      Mapping ECMaterial onto field" << endl;
				ECMaterialsVec->at(materialIndex).setToFieldReactionCoefficientByName(thisFieldName, coeff);
				hasToFieldInteractionsDefined[fieldIndex] = true;
				idxidxToFieldInteractionsDefined[fieldIndex].push_back(materialIndex);
			}
			else {
				cerr << "      Mapping field onto ECMaterial" << endl;
				ECMaterialsVec->at(materialIndex).setFromFieldReactionCoefficientByName(thisFieldName, coeff);
				hasFromFieldInteractionsDefined[materialIndex] = true;
				idxidxFromFieldInteractionsDefined[materialIndex].push_back(fieldIndex);
			}

		}

		for (int fldIdx = 0; fldIdx < numberOfFields; ++fldIdx) {
			if (hasToFieldInteractionsDefined[fldIdx]) idxToFieldInteractionsDefined.push_back(fldIdx);
		}
		for (int mtlIdx = 0; mtlIdx < numberOfMaterials; ++mtlIdx) {
			if (hasFromFieldInteractionsDefined[mtlIdx]) idxFromFieldInteractionsDefined.push_back(mtlIdx);
		}

		constructFieldReactionCoefficients();

	}

	if (xmlData->findElement("CellInteraction")) {
		CellInteractionsDefined = true;

		std::string methodName;
		std::string thisCellType;
		std::string thisCellTypeNew;
		int cellTypeIndex;
		int cellTypeNewIndex;
		for (int XMLidx = 0; XMLidx < CellInteractionXMLVec.size(); ++XMLidx) {

			cerr << "   Getting cell interaction " << XMLidx + 1 << endl;

			ASSERT_OR_THROW("A method must be defined for each cell interaction (using Method).", CellInteractionXMLVec[XMLidx]->findAttribute("Method"));
			methodName = CellInteractionXMLVec[XMLidx]->getAttribute("Method");
			cerr << "      Method " << methodName << endl;

			ASSERT_OR_THROW("An ECMaterial must be defined for each cell interaction (using ECMaterial).", CellInteractionXMLVec[XMLidx]->findElement("ECMaterial"));
			thisMaterialName = CellInteractionXMLVec[XMLidx]->getFirstElement("ECMaterial")->getText();
			cerr << "      ECMaterial " << thisMaterialName << endl;

			ASSERT_OR_THROW("A probability must be defined for each cell interaction (using Probability).", CellInteractionXMLVec[XMLidx]->findElement("Probability"));
			coeff = (float) CellInteractionXMLVec[XMLidx]->getFirstElement("Probability")->getDouble();
			cerr << "      Probability" << coeff << endl;

			ASSERT_OR_THROW("A cell type must be defined for each cell interaction (using CellType).", CellInteractionXMLVec[XMLidx]->findElement("CellType"));
			thisCellType = CellInteractionXMLVec[XMLidx]->getFirstElement("CellType")->getText();
			cerr << "      Cell type " << thisCellType << endl;

			if (methodName == "Differentiation") {
				ASSERT_OR_THROW("A new cell type must be defined for each cell differentiation interaction (using CellTypeNew).", CellInteractionXMLVec[XMLidx]->findElement("CellTypeNew"));
				thisCellTypeNew = CellInteractionXMLVec[XMLidx]->getFirstElement("CellTypeNew")->getText();
				cerr << "      New cell type " << thisCellTypeNew << endl;
			}

			// perform checks
			materialIndex = getECMaterialIndexByName(thisMaterialName);
			ASSERT_OR_THROW("ECMaterial not registered.", materialIndex >= 0);
			ASSERT_OR_THROW("Probability must be non-negative", coeff >= 0);
			cellTypeIndex = getCellTypeIndexByName(thisCellType);
			ASSERT_OR_THROW("Cell type not registered.", cellTypeIndex >= 0);
			cellTypeNewIndex = getCellTypeIndexByName(thisCellTypeNew);
			ASSERT_OR_THROW("Cell type not registered.", cellTypeNewIndex >= 0);

			if (methodName == "Proliferation") {
				ECMaterialsVec->at(materialIndex).setCellTypeCoefficientsProliferation(thisCellType, coeff);
			}
			else if (methodName == "Differentiation") {
				ECMaterialsVec->at(materialIndex).setCellTypeCoefficientsDifferentiation(thisCellType, thisCellTypeNew, coeff);
			}
			else if (methodName == "Death") {
				ECMaterialsVec->at(materialIndex).setCellTypeCoefficientsDeath(thisCellType, coeff);
			}
			else ASSERT_OR_THROW("Undefined cell response method: use Proliferation, Differentiation, or Death.", false);

			hasCellInteractionsDefined[materialIndex] = true;

		}

		for (int mtlIdx = 0; mtlIdx < numberOfMaterials; ++mtlIdx) if (hasCellInteractionsDefined[mtlIdx]) idxCellInteractionsDefined.push_back(mtlIdx);

		constructCellTypeCoefficients();
	}

	ECMaterialsInitialized = true;
	AnyInteractionsDefined = MaterialInteractionsDefined || FieldInteractionsDefined || CellInteractionsDefined;
}

void ECMaterialsSteppable::calculateCellInteractions(Field3D<ECMaterialsData *> *ECMaterialsFieldOld) {

	ASSERT_OR_THROW("ECMaterials steppable not yet initialized.", ECMaterialsInitialized);

	Neighbor neighbor;
	CellG * cell = 0;
	CellG *nCell = 0;

	int maxTypeId = (int)automaton->getMaxTypeId();
	CellInventory::cellInventoryIterator cInvItr;
	CellInventory * cellInventoryPtr = &potts->getCellInventory();

	int cellId;
	int cellType;
	std::vector<float> qtyOld(numberOfMaterials, 0.0);

	// Initialize information for counting cell interactions
	int numberOfCells = cellInventoryPtr->getSize();
	cellResponses.clear();
	float probAccumProlif;
	std::vector<float> probAccumDiff(maxTypeId, 0.0);
	float probAccumDeath;

	//		randomly order evaluation of responses
	std::vector<std::vector<int> > responseOrderSet(6, std::vector<int>(3, 0));
	responseOrderSet[0] = { 0, 1, 2 };
	responseOrderSet[1] = { 0, 2, 1 };
	responseOrderSet[2] = { 1, 0, 2 };
	responseOrderSet[3] = { 1, 2, 0 };
	responseOrderSet[4] = { 2, 0, 1 };
	responseOrderSet[5] = { 2, 1, 0 };
	std::vector<int> responseOrder = responseOrderSet[rand() % 6];

	// Loop over cells and consider each response
	Point3D pt;
	for (cInvItr = cellInventoryPtr->cellInventoryBegin(); cInvItr != cellInventoryPtr->cellInventoryEnd(); ++cInvItr) {
		cell = cellInventoryPtr->getCell(cInvItr);
		cellId = (int)cell->id;
		cellType = (int)cell->type;

		// Loop over cell boundaries to find interfaces with ECMaterials
		std::set<BoundaryPixelTrackerData > *boundarySet = boundaryTrackerPlugin->getPixelSetForNeighborOrderPtr(cell, neighborOrder);
		for (std::set<BoundaryPixelTrackerData >::iterator bInvItr = boundarySet->begin(); bInvItr != boundarySet->end(); ++bInvItr) {

			probAccumProlif = 0.0;
			probAccumDeath = 0.0;
			probAccumDiff.assign(maxTypeId, 0.0);

			// Loop over cell boundary neighbors and accumulate probabilities from medium sites
			pt = bInvItr->pixel;

			for (unsigned int nIdx = 0; nIdx <= nNeighbors; ++nIdx) {
				neighbor = boundaryStrategy->getNeighborDirect(const_cast<Point3D&>(pt), nIdx);

				if (!neighbor.distance) continue;

				nCell = cellFieldG->get(neighbor.pt);

				if (nCell) continue;

				qtyOld = ECMaterialsFieldOld->get(neighbor.pt)->getECMaterialsQuantityVec();

				for (std::vector<int>::iterator i = idxCellInteractionsDefined.begin(); i != idxCellInteractionsDefined.end(); ++i) {
					probAccumProlif += CellTypeCoefficientsProliferationByIndex[*i][cellType] * qtyOld[*i];
					probAccumDeath += CellTypeCoefficientsDeathByIndex[*i][cellType] * qtyOld[*i];
					for (int typeIdx = 0; typeIdx < maxTypeId; ++typeIdx) probAccumDiff[typeIdx] += CellTypeCoefficientsDifferentiationByIndex[*i][cellType][typeIdx];
				}

			}

		}

		// Consider each response in random order of response
		bool resp;
		std::string Action;
		std::string CellTypeDiff = "";
		for (int respIdx = 0; respIdx < 3; ++respIdx) {
			switch (responseOrder[respIdx]) {
			case 0 : // proliferation
				resp = probAccumProlif > randFloatGen01();
				Action = "Proliferate";
			case 1 : // death
				resp = probAccumDeath > randFloatGen01();
				Action = "Death";
			case 2 : // differentiation
				Action = "Differentiate";
				for (std::map<std::string, int>::iterator mitr = cellTypeNames.begin(); mitr != cellTypeNames.end(); ++mitr) {
					resp = probAccumDiff[mitr->second] > randFloatGen01();
					CellTypeDiff = mitr->first;
				}
			}
			if (resp) {
				cellResponses.push_back(ECMaterialsCellResponse(cell, Action, CellTypeDiff));
				break;
			}
		}

	}

}

void ECMaterialsSteppable::constructNeighborPtStorage() {

	neighborPtStorage.clear();
	neighborPtStorage.resize(fieldDim.x*fieldDim.y*fieldDim.z);

	boundaryStrategy = BoundaryStrategy::getInstance();
	int nNeighbors = boundaryStrategy->getMaxNeighborIndexFromNeighborOrder(1);

	Neighbor neighbor;
	Point3D pt(0, 0, 0);
	int ptIdx = 0;
	for (int z = 0; z < fieldDim.z; ++z) {
		for (int y = 0; y < fieldDim.y; ++y) {
			for (int x = 0; x < fieldDim.x; ++x) {
				pt = Point3D(x, y, z);
				neighborPtStorage[ptIdx].clear();
				neighborPtStorage[ptIdx].reserve(nNeighbors + 1);

				for (unsigned int nIdx = 0; nIdx <= nNeighbors; ++nIdx) {
					neighbor = boundaryStrategy->getNeighborDirect(const_cast<Point3D&>(pt), nIdx);

					if (!neighbor.distance) continue;

					neighborPtStorage[ptIdx].push_back(neighbor.pt);
				}

				++ptIdx;

			}
		}
	}

}

void ECMaterialsSteppable::constructFieldReactionCoefficients() {

	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();
	numberOfFields = fieldVec.size();

	toFieldReactionCoefficientsByIndex = std::vector<std::vector<float> >(numberOfFields, std::vector<float>(numberOfMaterials, 0.0));
	fromFieldReactionCoefficientsByIndex = std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(numberOfFields, 0.0));

	for (std::vector<int>::iterator i = idxFromFieldInteractionsDefined.begin(); i != idxFromFieldInteractionsDefined.end(); ++i) {
		for (std::vector<int>::iterator j = idxidxFromFieldInteractionsDefined[*i].begin(); j != idxidxFromFieldInteractionsDefined[*i].end(); ++j) {
			fromFieldReactionCoefficientsByIndex[*i][*j] = ECMaterialsVec->at(*i).getFromFieldReactionCoefficientByName(getFieldNameByIndex(*j));
		}
	}
	for (std::vector<int>::iterator i = idxToFieldInteractionsDefined.begin(); i != idxToFieldInteractionsDefined.end(); ++i) {
		for (std::vector<int>::iterator j = idxidxToFieldInteractionsDefined[*i].begin(); j != idxidxToFieldInteractionsDefined[*i].end(); ++j) {
			toFieldReactionCoefficientsByIndex[*i][*j] = ECMaterialsVec->at(*j).getToFieldReactionCoefficientByName(getFieldNameByIndex(*i));
		}
	}

}

void ECMaterialsSteppable::constructMaterialReactionCoefficients() {

	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();

	materialReactionCoefficientsByIndex = std::vector<std::vector<std::vector<float> > >(numberOfMaterials, std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(2, 0.0)));

	for (std::vector<int>::iterator i = idxMaterialReactionsDefined.begin(); i != idxMaterialReactionsDefined.end(); ++i) {
		for (std::vector<int>::iterator j = idxidxMaterialReactionsDefined[*i].begin(); j != idxidxMaterialReactionsDefined[*i].end(); ++j) {
			materialReactionCoefficientsByIndex[*i][*j] = ECMaterialsVec->at(*i).getMaterialReactionCoefficientsByName(ECMaterialsVec->at(*j).getName());
		}
	}
}

void ECMaterialsSteppable::constructCellTypeCoefficients() {

	int maxTypeId = (int)automaton->getMaxTypeId();
	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();

	cellTypeNames.clear();

	CellTypeCoefficientsProliferationByIndex = std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(maxTypeId, 0.0));
	CellTypeCoefficientsDifferentiationByIndex = std::vector<std::vector<std::vector<float> > >(numberOfMaterials, std::vector<std::vector<float> >(maxTypeId, std::vector<float>(maxTypeId, 0.0)));
	CellTypeCoefficientsDeathByIndex = std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(maxTypeId, 0.0));

	for (std::vector<int>::iterator i = idxCellInteractionsDefined.begin(); i != idxCellInteractionsDefined.end(); ++i) {
		for (int j = 0; j < maxTypeId; ++j) {

			std::string cellTypeName = automaton->getTypeName(j);

			cellTypeNames.insert(make_pair(cellTypeName, j));

			CellTypeCoefficientsProliferationByIndex[*i][j] = ECMaterialsVec->at(*i).getCellTypeCoefficientsProliferation(cellTypeName);
			CellTypeCoefficientsDeathByIndex[*i][j] = ECMaterialsVec->at(*i).getCellTypeCoefficientsDeath(cellTypeName);
			for (int k = 0; k < maxTypeId; ++k) {
				CellTypeCoefficientsDifferentiationByIndex[*i][j][k] = ECMaterialsVec->at(*i).getCellTypeCoefficientsDifferentiation(cellTypeName, automaton->getTypeName(k));
			}
		}
	}

}

void ECMaterialsSteppable::constructCellTypeCoefficientsProliferation() {

	int maxTypeId = (int)automaton->getMaxTypeId();
	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();

	CellTypeCoefficientsProliferationByIndex = std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(maxTypeId, 0.0));

	for (std::vector<int>::iterator i = idxCellInteractionsDefined.begin(); i != idxCellInteractionsDefined.end(); ++i) {
		for (int j = 0; j < maxTypeId; ++j) {
			CellTypeCoefficientsProliferationByIndex[*i][j] = ECMaterialsVec->at(*i).getCellTypeCoefficientsProliferation(automaton->getTypeName(j));
		}
	}

}

void ECMaterialsSteppable::constructCellTypeCoefficientsDifferentiation() {

	int maxTypeId = (int)automaton->getMaxTypeId();
	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();

	CellTypeCoefficientsDifferentiationByIndex = std::vector<std::vector<std::vector<float> > >(numberOfMaterials, std::vector<std::vector<float> >(maxTypeId, std::vector<float>(maxTypeId, 0.0)));

	for (std::vector<int>::iterator i = idxCellInteractionsDefined.begin(); i != idxCellInteractionsDefined.end(); ++i) {
		for (int j = 0; j < maxTypeId; ++j) {
			for (int k = 0; k < maxTypeId; ++k) {
				CellTypeCoefficientsDifferentiationByIndex[*i][j][k] = ECMaterialsVec->at(*i).getCellTypeCoefficientsDifferentiation(automaton->getTypeName(j), automaton->getTypeName(k));
			}
		}
	}

}

void ECMaterialsSteppable::constructCellTypeCoefficientsDeath() {

	int maxTypeId = (int)automaton->getMaxTypeId();
	numberOfMaterials = ecMaterialsPlugin->getNumberOfMaterials();

	CellTypeCoefficientsDeathByIndex = std::vector<std::vector<float> >(numberOfMaterials, std::vector<float>(maxTypeId, 0.0));

	for (std::vector<int>::iterator i = idxCellInteractionsDefined.begin(); i != idxCellInteractionsDefined.end(); ++i) {
		for (int j = 0; j < maxTypeId; ++j) {
			CellTypeCoefficientsDeathByIndex[*i][j] = ECMaterialsVec->at(*i).getCellTypeCoefficientsDeath(automaton->getTypeName(j));
		}
	}

}

void ECMaterialsSteppable::calculateMaterialToFieldInteractions(const Point3D &pt, std::vector<float> _qtyOld) {
	float thisFieldVal;
	for (std::vector<int>::iterator i = idxToFieldInteractionsDefined.begin(); i != idxToFieldInteractionsDefined.end(); ++i) {
		thisFieldVal = fieldVec[*i]->get(pt);
		for (std::vector<int>::iterator j = idxidxToFieldInteractionsDefined[*i].begin(); j != idxidxToFieldInteractionsDefined[*i].end(); ++j) {
			thisFieldVal += toFieldReactionCoefficientsByIndex[*i][*j] * _qtyOld[*j];
		}
		if (thisFieldVal < 0.0) thisFieldVal = 0.0;
		fieldVec[*i]->set(pt, thisFieldVal);
	}
}

std::string ECMaterialsSteppable::toString(){
   return "ECMaterialsSteppable";
}

std::string ECMaterialsSteppable::steerableName(){
   return toString();
}