"""wine_behavior_tracker — Wine behavior capture + Wine/MiniWin differential evidence.

Schema v1 JSONL events; every field is honest (absent stays absent).
"""

__version__ = "1.0.0"

EVENT_TYPES = [
    "process_start", "process_exit", "module_load", "module_load_fail",
    "api_call", "api_return", "fs_effect", "reg_effect", "window_event",
    "message_event", "gdi_op", "screenshot", "workflow_checkpoint",
    "parser_error", "run_meta",
]

# v1 schema fields (design-v1 names preserved). Unknown fields stay unknown:
# the validator rejects nothing it does not recognize, it only records what
# each event actually carries.
SCHEMA_FIELDS = [
    "run_id", "timestamp", "sequence", "process", "thread", "event_type",
    "module", "api", "arguments", "return_value", "last_error", "call_site",
    "stack", "related_artifact", "capture_source", "confidence",
]

CONFIDENCE_LEVELS = ("raw", "derived", "inferred")
