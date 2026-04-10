"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, request


class ReservedSlugConflictTest(LinkManagementIntegrationBase):
    port = 38309

    def test_reserved_slug_rejected_with_409(self):
        """
        [Integration][Link Management] Reserved slug rejection is enforced.
        
        Scenario:
            Given POST /api/v1/links with reserved slug variant API.
            When create is attempted.
            Then response is 409 reserved_slug.
        
        API/Feature covered:
            - Stage 2 case-insensitive reserved slug denylist.
        
        If this breaks, first check:
            - Reserved slug normalization.
            - Conflict code generation for reserved values.
        """
        status, payload, _ = request(
            self.port,
            "POST",
            "/api/v1/links",
            body='{"url":"https://example.com/r","slug":"API"}',
            headers={"Content-Type": "application/json"},
        )
        self.assertEqual(409, status)
        self.assertIn('"reserved_slug"', payload)


if __name__ == "__main__":
    unittest.main()
