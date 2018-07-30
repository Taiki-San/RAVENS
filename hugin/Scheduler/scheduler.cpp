//
// Created by Emile-Hugo Spir on 3/14/18.
//

#include <cstring>
#include <chrono>
#include "scheduler.h"

size_t _realBlockSizeBit = BLOCK_SIZE_BIT_DEFAULT;
size_t _realFullAddressSpace = FLASH_SIZE_BIT_DEFAULT;

void schedule(const vector<BSDiffMoves> & input, vector<PublicCommand> & output, bool printStats)
{
	vector<Block> blockStructure;

	if(!buildBlockVector(input, blockStructure))
		return;

	SchedulerData scheduler;

	scheduler.wantLog = printStats;

	//This pass is redundant with removeUnidirectionnalReferences but is a bit faster as less complicated
	Scheduler::removeSelfReferencesOnly(blockStructure, scheduler);

	Scheduler::removeUnidirectionnalReferences(blockStructure, scheduler);

	Scheduler::removeNetworks(blockStructure, scheduler);

	scheduler.generateInstructions(output);

	if(printStats)
		scheduler.printStats(output);
}

#include "bsdiff/bsdiff.h"
#include "validation.h"

void trimBSDiff(vector<BSDiffPatch> &patch)
{
	BSDiffPatch & lastPatch = patch.back();
	if(lastPatch.lengthExtra == 0)
	{
		size_t trim = lastPatch.lengthDelta;
		while(trim != 0 && lastPatch.deltaData[--trim] == 0);

		//Extra padding present, we can trim it!
		if(trim != lastPatch.lengthDelta - 1)
		{
			//We may trim, but some data are still left
			if(trim != 0)
				lastPatch.lengthDelta = trim + 1;

				//No data left, the last patch is pointless
			else
				patch.pop_back();
		}
	}
}

bool generatePatch(const uint8_t *original, size_t originalLength, const uint8_t *newer, size_t newLength, SchedulerPatch &outputPatch, bool printStats)
{
	outputPatch.clear(false);

	//We look for an identical prefix
	size_t earlySkip = 0;
	for(size_t smallest = MIN(originalLength, newLength);
				earlySkip < smallest && original[earlySkip] == newer[earlySkip];
				++earlySkip);

	//We won't have to diff this part
	earlySkip &= BLOCK_MASK;
	outputPatch.startAddress = earlySkip >> BLOCK_SIZE_BIT;

	vector<BSDiffPatch> patch;

	//Generate the diff
	//TODO: Ignore delta for less than a couple of bytes, too wasteful in COPY encoding
	//TODO: Introduce a skip field, to go over vast untouched area faster
	{
#ifdef PRINT_SPEED
		auto beginBSDiff = chrono::high_resolution_clock::now();
#endif
		bsdiff(original + earlySkip, originalLength - earlySkip, newer + earlySkip, newLength - earlySkip, patch);
#ifdef PRINT_SPEED
		auto endBSDiff = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endBSDiff - beginBSDiff).count();
		cout << "Performing BSDiff in " << duration << " ms." << endl;
#endif
	}

	if(earlySkip)
	{
		for(auto & diff : patch)
			diff.oldDataAddress += earlySkip;
	}

	//Before processing the diff, we check it actually works
	if(!validateBSDiff(original, originalLength, newer, newLength, patch, earlySkip))
		return false;

	//If we don't have extra at the end, we may be able to trim the delta.
	trimBSDiff(patch);

	if(printStats)
	{
		size_t newData = 0;

		for(auto diff : patch)
			newData += diff.lengthExtra;

		cout << "Valid BSDiff with " << newData << " bytes of new data" << endl;
	}

	if(patch.empty())
	{
		cout << "Files are identical. If not the case, please open a bug report." << endl;
		return true;
	}

	//Craft the BSDiffMoves (the moves to perform)
	vector<BSDiffMoves> moves;
	moves.reserve(patch.size());

	size_t currentAddress = earlySkip;
	for(const auto & cur : patch)
	{
		if(cur.lengthDelta == 0)
			continue;

		moves.emplace_back(BSDiffMoves(cur.oldDataAddress, cur.lengthDelta, currentAddress));
		currentAddress += cur.lengthDelta + cur.lengthExtra;

		outputPatch.bsdiff.push_back(BSDiff {
				.delta = {
						.data = cur.deltaData,
						.length = cur.lengthDelta
				},

				.extra = {
						.data = cur.extraData,
						.length = cur.lengthExtra
				}
		});
	}

	//Extend the last copy if we had to trim it so that the end of the last block is copied
	if(currentAddress < newLength)
	{
		assert(!moves.empty());
		moves.back().length += MIN(newLength - currentAddress, BLOCK_SIZE - (currentAddress & BLOCK_OFFSET_MASK));
	}

	if(outputPatch.bsdiff.empty())
		return false;

	//Generate the commands to run
	{
#ifdef PRINT_SPEED
		auto begin = chrono::high_resolution_clock::now();
#endif
		schedule(moves, outputPatch.commands, printStats);
#ifdef PRINT_SPEED
		auto end = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
		cout << "Performing conflict resolution in " << duration << " ms." << endl;
#endif
	}

	{
#ifdef PRINT_SPEED
		auto begin = chrono::high_resolution_clock::now();
#endif
		generateVerificationRangesPrePatch(outputPatch, earlySkip);
		generateVerificationRangesPostPatch(outputPatch, earlySkip, newLength);
#ifdef PRINT_SPEED
		auto end = chrono::high_resolution_clock::now();
		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
		cout << "Generating conflict ranges in " << duration << " ms." << endl;
#endif
	}

	computeExpectedHashForRanges(outputPatch.oldRanges, original, originalLength);
	computeExpectedHashForRanges(outputPatch.newRanges, newer, newLength);

	return true;
}