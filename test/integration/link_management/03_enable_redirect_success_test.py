"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class EnableRedirectSuccessTest(LinkManagementIntegrationBase):
    port = 38303

    def test_reenable_then_redirect_succeeds(self):
        """
        [Integration][Link Management] Enable endpoint restores redirect behavior.
        
        Scenario:
            Given a link disabled via management endpoint.
            When POST /api/v1/links/{slug}/enable is called.
            Then GET /{slug} returns 302 and correct Location target.
        
        API/Feature covered:
            - Stage 2 enable operation.
            - Redirect resume after re-enable.
        
        If this breaks, first check:
            - Enable mutation path.
            - Active-state branch in redirect handler.
        """
        status, _, _ = create_link(self.port, "lmenb01", "https://example.com/enabled")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "POST", "/api/v1/links/lmenb01/disable")
        self.assertEqual(200, status)
        status, _, _ = request(self.port, "POST", "/api/v1/links/lmenb01/enable")
        self.assertEqual(200, status)

        status, _, res = request(self.port, "GET", "/lmenb01")
        self.assertEqual(302, status)
        self.assertEqual("https://example.com/enabled", res.getheader("Location"))


if __name__ == "__main__":
    unittest.main()
