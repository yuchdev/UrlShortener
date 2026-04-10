"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class PreviewStatusMatrixTest(LinkManagementIntegrationBase):
    port = 38307

    def test_preview_returns_active_disabled_expired_deleted(self):
        """
        [Integration][Link Management] Preview endpoint reports lifecycle status matrix.
        
        Scenario:
            Given active, disabled, expired, and deleted links.
            When GET /api/v1/links/{slug}/preview is called for each.
            Then response status field is active/disabled/expired/deleted as expected.
        
        API/Feature covered:
            - Stage 2 preview endpoint lifecycle projection.
        
        If this breaks, first check:
            - resolveLinkStatus() usage in preview handler.
            - status string mapping in preview payload.
        """
        status, _, _ = create_link(self.port, "lmprev1", "https://example.com/a")
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmprev1/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"active"', payload)

        status, _, _ = create_link(self.port, "lmprev2", "https://example.com/d")
        self.assertEqual(201, status)
        request(self.port, "POST", "/api/v1/links/lmprev2/disable")
        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmprev2/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"disabled"', payload)

        status, _, _ = create_link(self.port, "lmprev3", "https://example.com/e", '"expires_at":"2000-01-01T00:00:00Z"')
        self.assertEqual(201, status)
        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmprev3/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"expired"', payload)

        status, _, _ = create_link(self.port, "lmprev4", "https://example.com/x")
        self.assertEqual(201, status)
        request(self.port, "DELETE", "/api/v1/links/lmprev4")
        status, payload, _ = request(self.port, "GET", "/api/v1/links/lmprev4/preview")
        self.assertEqual(200, status)
        self.assertIn('"status":"deleted"', payload)


if __name__ == "__main__":
    unittest.main()
