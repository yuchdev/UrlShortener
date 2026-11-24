import os
import sys
import time
import unittest

CURRENT_DIR = os.path.dirname(__file__)
LINK_MGMT_DIR = os.path.abspath(os.path.join(CURRENT_DIR, "..", "link_management"))
if LINK_MGMT_DIR not in sys.path:
    sys.path.insert(0, LINK_MGMT_DIR)

from link_management_common import LinkManagementIntegrationBase, request
class AnalyticsStatsValidation(LinkManagementIntegrationBase):
    port=38324
    def test_unknown_slug_policy(self):
        now = int(time.time())
        # Bounded +-1h window: keeps the span under the T-76 10,000-bucket
        # cap (04.0/03) while still bracketing the just-recorded event.
        status, payload, _ = request(
            self.port, "GET",
            f"/api/v1/links/nope/stats?from={now - 3600}&to={now + 3600}&bucket=hour"
        )
        self.assertEqual(200, status)
        self.assertIn('"slug"', payload)
        self.assertIn('"total_attempts"', payload)
if __name__=='__main__': unittest.main()
