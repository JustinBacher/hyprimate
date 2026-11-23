#!/usr/bin/env python3
"""Exercise collector attribution and session boundaries without compositor dependencies."""
import json
import subprocess
import sys
import unittest

PROBE = sys.argv.pop(1)
CASES = {
    item["case"]: item["report"]
    for item in map(json.loads, subprocess.check_output([PROBE], text=True).splitlines())
}


def row(report, style):
    return next(item for item in report["styles"] if item["style"] == style)


class LiveProfilerTests(unittest.TestCase):
    def test_disabled_and_reset_do_not_capture(self):
        self.assertFalse(CASES["disabled"]["enabled"])
        self.assertEqual(CASES["disabled"]["styles"], [])
        self.assertEqual(CASES["new_session"]["styles"], [])
        self.assertEqual(CASES["stale_scope"]["styles"], [])
        self.assertEqual(CASES["reset_stopped"]["styles"], [])
        self.assertFalse(CASES["reset_stopped"]["enabled"])

    def test_styles_domains_phases_and_workers_remain_separate(self):
        report = CASES["measured"]
        paper, smoke, cube = (row(report, style) for style in ("paper", "smoke", "cube"))
        self.assertEqual((paper["domain"], paper["operation"]), ("window", "close"))
        self.assertEqual((cube["domain"], cube["operation"]), ("workspace", "transition"))
        self.assertEqual(paper["gpu"]["p50_ms"], 2)
        self.assertEqual(smoke["gpu"]["p50_ms"], 4)
        self.assertEqual(cube["gpu"]["p50_ms"], 6)
        for phase in ("update", "worker"):
            self.assertEqual(paper["cpu"][phase]["thread_cpu"]["samples"], 1)
            self.assertGreater(paper["cpu"][phase]["thread_cpu"]["mean_ms"], 0)
            self.assertEqual(paper["cpu"][phase]["wall"]["samples"], 1)
        self.assertEqual(smoke["cpu"]["update"]["thread_cpu"]["samples"], 0)

    def test_pacing_deduplicates_passes_and_excludes_idle_and_other_outputs(self):
        paper = row(CASES["measured"], "paper")
        self.assertEqual(paper["drawn_frames"], 5)
        self.assertEqual(paper["presentation_events"], 5)
        self.assertEqual(paper["draw_intervals"]["samples"], 2)
        self.assertEqual(paper["presentation_intervals"]["samples"], 2)
        self.assertEqual(paper["presentation_intervals"]["max_ms"], 30)
        self.assertEqual(paper["late_presentation_intervals"], 1)
        self.assertEqual(paper["estimated_missed_refreshes"], 2)

    def test_stopping_freezes_samples(self):
        self.assertEqual(CASES["stopped"], CASES["stopped_again"])
        self.assertFalse(CASES["stopped"]["enabled"])
        self.assertEqual(CASES["measured"]["styles"], CASES["stopped"]["styles"])

    def test_memory_is_explicitly_shared_or_lower_bound(self):
        report = CASES["measured"]
        self.assertTrue(report["memory"]["available"])
        self.assertGreater(report["memory"]["baseline_rss_bytes"], 0)
        memory = row(report, "paper")["memory"]
        self.assertGreater(memory["process_rss_observed_max_bytes"], 0)
        self.assertEqual(memory["resource_samples"], 2)
        self.assertEqual(memory["known_cpu_resource_peak_bytes"], 1000)
        self.assertEqual(memory["known_gpu_resource_peak_bytes"], 3000)
        self.assertTrue(any("entire compositor" in note for note in report["notes"]))

    def test_retained_percentiles_and_session_totals_have_distinct_scopes(self):
        report = CASES["bounded"]
        gpu = row(report, "paper")["gpu"]
        self.assertEqual(gpu["samples"], 600)
        self.assertEqual(gpu["retained_samples"], 512)
        self.assertEqual(gpu["p50_ms"], 343)
        self.assertEqual(gpu["mean_ms"], 299.5)
        self.assertEqual(gpu["max_ms"], 599)
        self.assertEqual(report["gpu_dropped_samples"], 1)
        self.assertEqual(report["gpu_disjoint_events"], 1)
        self.assertEqual(report["gpu_status"], "disjoint")

    def test_rejected_presentation_breaks_interval_sequence(self):
        report = CASES["rejected"]
        self.assertEqual(report["rejected_presentation_events"], 1)
        self.assertEqual(row(report, "paper")["presentation_intervals"]["samples"], 0)


    def test_untimestamped_success_uses_separate_callback_series(self):
        report = CASES["untimestamped"]
        paper = row(report, "paper")
        self.assertEqual(report["rejected_presentation_events"], 0)
        self.assertEqual(paper["presentation_events"], 3)
        self.assertEqual(paper["untimestamped_presentation_events"], 3)
        self.assertEqual(paper["presentation_intervals"]["samples"], 0)
        self.assertEqual(paper["presentation_callback_intervals"]["samples"], 2)

    def test_unassociated_output_events_cannot_inherit_a_style(self):
        paper = row(CASES["unassociated"], "paper")
        self.assertEqual(paper["presentation_events"], 2)
        self.assertEqual(paper["presentation_intervals"]["samples"], 0)
        self.assertEqual(paper["presentation_callback_intervals"]["samples"], 0)

if __name__ == "__main__":
    unittest.main()
