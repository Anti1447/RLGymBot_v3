// ===== CORRECTED REANALYSIS CODE FOR PPOLearner.cpp =====
// Insert this code after the criticLoss computation (around line 322)
// and before the KL divergence computation

				// ===== REANALYSIS LOSS (Phase 6) =====
				if (reanalysisTargets && reanalysisTargets->hasTargets) {
					// Move reanalysis targets to device
					torch::Tensor reanalysisObs = reanalysisTargets->observations.to(device);
					torch::Tensor mctsValueTargets = reanalysisTargets->mctsValueTargets.to(device);
					torch::Tensor mctsPolicyTargets = reanalysisTargets->mctsPolicyTargets.to(device);

					// Get action count from the policy targets shape
					int numActions = static_cast<int>(mctsPolicyTargets.size(1));

					// Get current policy and value predictions for reanalysis states
					torch::Tensor reanalysisActionMasks = torch::ones({ reanalysisObs.size(0), numActions }, device);

					// Policy reanalysis loss
					torch::Tensor policyReanalysisLoss;
					if (trainPolicy) {
						torch::Tensor reanalysisPolicyProbs = InferPolicyProbsFromModels(models, reanalysisObs, reanalysisActionMasks, config.policyTemperature, false);

						// KL divergence: KL(MCTS || Policy) = sum(MCTS * log(MCTS / Policy))
						torch::Tensor logPolicyProbs = torch::log(reanalysisPolicyProbs + 1e-8f);
						torch::Tensor logMCTSProbs = torch::log(mctsPolicyTargets + 1e-8f);
						policyReanalysisLoss = torch::sum(
							mctsPolicyTargets * (logMCTSProbs - logPolicyProbs),
							-1
						).mean() * batchSizeRatio;
					}

					// Value reanalysis loss
					torch::Tensor valueReanalysisLoss;
					if (trainCritic) {
						torch::Tensor reanalysisValuePreds = InferCritic(reanalysisObs).squeeze(-1);
						valueReanalysisLoss = torch::mse_loss(reanalysisValuePreds, mctsValueTargets) * batchSizeRatio;
					}

					// Add weighted reanalysis losses
					if (trainPolicy && policyReanalysisLoss.defined()) {
						ppoLoss = ppoLoss + config.policyReanalysisWeight * policyReanalysisLoss;
					}
					if (trainCritic && valueReanalysisLoss.defined()) {
						criticLoss = criticLoss + config.valueReanalysisWeight * valueReanalysisLoss;
					}

					// Track metrics (only once per epoch, not per minibatch)
					if (start == 0) {
						if (policyReanalysisLoss.defined())
							report["Reanalysis/Policy Loss"] = policyReanalysisLoss.detach().cpu().item<float>();
						if (valueReanalysisLoss.defined())
							report["Reanalysis/Value Loss"] = valueReanalysisLoss.detach().cpu().item<float>();
					}
				}
				// ===== END REANALYSIS LOSS =====
