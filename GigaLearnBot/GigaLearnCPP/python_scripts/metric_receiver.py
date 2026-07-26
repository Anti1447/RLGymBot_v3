import site
import sys
import json
import os

wandb_run = None

# Takes in the python executable path, the three wandb init strings, and optionally the current run ID
# Returns the ID of the run (either newly created or resumed)
def init(py_exec_path, project, group, name, id = None):

	global wandb_run
	
	# Fix the path of our interpreter so wandb doesn't run RLGym_PPO instead of Python
	# Very strange fix for a very strange problem
	sys.executable = py_exec_path
	
	try:
		site_packages_dir = os.path.join(os.path.join(os.path.dirname(py_exec_path), "Lib"), "site-packages")
		sys.path.append(site_packages_dir)
		site.addsitedir(site_packages_dir)
		import wandb
	except Exception as e:
		raise Exception(f"""
			FAILED to import wandb! Make sure GigaLearnCPP isn't using the wrong Python installation.
			This installation's site packages: {site.getsitepackages()}
			Exception: {repr(e)}"""
		)
	
	print("Calling wandb.init()...")
	if not (id is None) and len(id) > 0:
		wandb_run = wandb.init(project = project, group = group, name = name, id = id, resume = "allow")
	else:
		wandb_run = wandb.init(project = project, group = group, name = name)
	return wandb_run.id

def add_metrics(metrics):
	global wandb_run
	wandb_run.log(metrics)

def on_checkpoint_saved(checkpoint_path, checkpoints_root, total_timesteps):
	"""Called from C++ Learner::Save() after REWARD_DIAGNOSIS.json is written.
	Runs automated reward analysis comparing the 2 most recent checkpoints.
	Logs summary metrics to wandb and saves graphs locally. Non-fatal."""
	global wandb_run
	try:
		# Import reward_analysis_auto from same directory or parent directory
		import sys
		from pathlib import Path

		# Add parent directory to path to find python_scripts module
		script_dir = Path(__file__).parent
		sys.path.insert(0, str(script_dir))

		import reward_analysis_auto
		reward_analysis_auto.run_analysis(
			checkpoint_path=checkpoint_path,
			checkpoints_root=checkpoints_root,
			total_timesteps=total_timesteps,
			wandb_run=wandb_run
		)
	except Exception as e:
		print(f"[RewardAnalysis] WARNING: Analysis failed (non-fatal): {e}")
		import traceback
		traceback.print_exc()
