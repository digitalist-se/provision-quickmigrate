<?php
/**
 * @file
 * Main drush command file for quick migrate.
 */

/**
 * Implements hook_drush_command().
 */
function quickmigrate_drush_command() {
  $items = array();

  $items['provision-quickmigrate'] = array(
    'description' => 'Do a quick migration.',
    'callback' => 'drush_provision_quickmigrate',
    'arguments' => array(
      '@platform_name' => dt('The Drush alias of the platform.')),
    'examples' => array(
      'drush @site provision-migrate @platform_name' => 'Migrate the site as defined by the Drush alias, to the platform as defined by the platform\'s Drush alias',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
  );
  return $items;
}

/**
 * Perform the actual migration.
 */
function drush_provision_quickmigrate($platform, $new_uri = NULL) {
  // Put the site under maintenance.
  drush_log(dt("Putting site under maintenance"));
  d('@self')->site_enabled = FALSE;
  _provision_drupal_create_settings_file();

  drush_set_option('old_platform', d()->platform->name);

  // If we are renaming the site, pick this up early.
  if (!is_null($new_uri) && ($new_uri != d()->uri)) {
    drush_set_option('target_name', $new_uri);
  }
  else {
    // the site url stays the same
    drush_set_option('target_name', '@' . d()->uri);
  }

  $target = drush_get_option('target_name');
  $options = array();
  $options['uri'] = d()->uri;
  if (!is_null($new_uri) && ($new_uri != d()->uri)) {
    $options = d()->options;
    $options['uri'] = ltrim($new_uri, '@');
    $hash_name = drush_get_option('#name') ? '#name' : 'name';
    $options[$hash_name] = $new_uri;
  }
  $options['platform'] = $platform;
  $options['root'] = d($platform)->root;

  if ($profile = drush_get_option('profile', FALSE)) {
    $options['profile'] = $profile;
  }

  if ($db_server = drush_get_option('new_db_server', FALSE)) {
    $options['db_server'] = $db_server;
  }
  $new_path = d($platform)->root . "/sites/{$options['uri']}";
  drush_set_option('new_site_path', $new_path);
  $old_path = d()->site_path;
  rename($old_path, $new_path);
  drush_backend_invoke_args('provision-save', array($target), $options);
  provision_backend_invoke($target, 'updatedb');
  provision_backend_invoke($target, 'cc all');
  provision_backend_invoke('@hostmaster', 'hosting-import', array($target));
  if (!drush_get_error()) {
    drush_log(dt("Bringing site out of maintenance"));
    d()->site_enabled = true;
    drush_set_option('provision_save_config', false);
    provision_backend_invoke($target, 'provision-verify');
  }
}

/**
 * Delete the old site directory and recreate the settings file
 */
function drush_quickmigrate_post_provision_quickmigrate($platform, $new_uri = null) {
  drush_set_option('installed', FALSE);

  if (!drush_get_option('deploy_replace_site', FALSE)) {
    // we remove the aliases even if redirection is enabled as a precaution
    // if redirection is enabled, keep silent about errors
    _provision_drupal_delete_aliases();
    _provision_recursive_delete(d()->site_path);
    d()->service('http')->sync(d()->site_path);
  }

  if (!is_null($new_uri) && ($new_uri != d()->uri)) {
    // remove the existing alias
    $config = new provisionConfig_drushrc_alias(d()->name);
    $config->unlink();
  }

  // Load the config file of the newly migrated site and return it to hosting.
  provision_reload_config('site', drush_get_option('new_site_path', d()->site_path) . '/drushrc.php');
}


/**
 * Fix bug in provision that doesn't put the site in maintenance mode.
 */
function quickmigrate_provision_drupal_config($uri, $data) {
  var_dump('ugh');
  if (!d()->site_enabled) {
    return array('$conf[\'maintenance_mode\'] = TRUE;');
  }
}
