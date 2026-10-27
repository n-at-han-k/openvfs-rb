# frozen_string_literal: true

require_relative "../openvfs"
require "openvfs/occ/occ"

module OpenVFS
  # OpenCloud Desktop's upload jobs.
  module OCC
    class Account
      # Upload a file that does not yet exist remotely.
      # @parameter local_path [String] The local file to stream.
      # @parameter remote_path [String] The path relative to the account's WebDAV root.
      # @parameter timeout_ms [Integer] The upload deadline in milliseconds.
      # @returns [UploadResult] The server response and uploaded file metadata.
      def upload_new(local_path, remote_path, timeout_ms: 300_000)
        PUTFileJob.new(self, local_path, remote_path, nil, true, timeout_ms).run
      end

      # Upload changes to the specified remote revision.
      # @parameter local_path [String] The local file to stream.
      # @parameter remote_path [String] The path relative to the account's WebDAV root.
      # @parameter etag [String] The previously observed remote ETag.
      # @parameter timeout_ms [Integer] The upload deadline in milliseconds.
      # @returns [UploadResult] The server response and uploaded file metadata.
      def upload_changed(local_path, remote_path, etag:, timeout_ms: 300_000)
        if etag.nil? || etag.empty?
          raise ArgumentError, "etag is required when uploading changes"
        end
        PUTFileJob.new(self, local_path, remote_path, etag, false, timeout_ms).run
      end
    end

    class PUTFileJob
      # Start the upload and wait for its response.
      # @returns [UploadResult] The completed upload result.
      def run
        start
        wait
      end
    end
  end
end
